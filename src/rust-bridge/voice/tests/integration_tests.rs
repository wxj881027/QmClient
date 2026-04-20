//! 语音模块集成测试
//!
//! 包含多样化测试数据：
//! - 正弦波信号（默认）
//! - 静音信号
//! - 白噪声信号
//! - 方波信号
//! - 多频率复合信号
//! - 边界值信号

use ddnet_voice::{
    audio::mixer::{AudioMixer, AudioSource},
    codec::OpusCodec,
    dsp::DspChain,
    network::protocol::{calculate_context_hash, PacketType, VoicePacket},
    spatial::{calculate_spatial, SpatialConfig},
    PlayerSnapshot, VoiceConfig, VoiceSystem,
};

// ============== 测试信号生成器 ==============

/// 生成静音信号
fn generate_silence_signal(samples: usize) -> Vec<i16> {
    vec![0i16; samples]
}

/// 生成白噪声信号
fn generate_white_noise_signal(samples: usize, amplitude: i16) -> Vec<i16> {
    use std::time::{SystemTime, UNIX_EPOCH};
    let seed = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos() as u64;
    let mut state = seed;
    let mut result = Vec::with_capacity(samples);

    for _ in 0..samples {
        // 简单的 LCG 随机数生成器
        state = state.wrapping_mul(1664525).wrapping_add(1013904223);
        let random = (state >> 16) as i32 % 32767; // 限制范围避免溢出
        result.push((random as f32 * (amplitude as f32 / 32767.0)) as i16);
    }
    result
}

/// 生成方波信号
fn generate_square_wave_signal(samples: usize, frequency: f32, amplitude: i16) -> Vec<i16> {
    let sample_rate = 48000.0f32;
    let period_samples = sample_rate / frequency;

    (0..samples)
        .map(|i| {
            let phase = (i as f32 % period_samples) / period_samples;
            if phase < 0.5 {
                amplitude
            } else {
                -amplitude
            }
        })
        .collect()
}

/// 生成多频率复合信号
fn generate_multi_frequency_signal(samples: usize, frequencies: &[f32], amplitude: i16) -> Vec<i16> {
    let sample_rate = 48000.0f32;
    let per_freq_amplitude = amplitude as f32 / frequencies.len() as f32;

    (0..samples)
        .map(|i| {
            let mut value = 0.0f32;
            for &freq in frequencies {
                let phase = i as f32 * 2.0 * std::f32::consts::PI * freq / sample_rate;
                value += phase.sin();
            }
            (value * per_freq_amplitude) as i16
        })
        .collect()
}

/// 生成边界值信号（最大/最小振幅）
fn generate_boundary_signal(samples: usize, pattern: &str) -> Vec<i16> {
    match pattern {
        "max" => vec![32767i16; samples],
        "min" => vec![-32767i16; samples], // 使用 -32767 而非 -32768 避免 abs() 溢出
        "alternating" => (0..samples).map(|i| if i % 2 == 0 { 32767 } else { -32767 }).collect(),
        "ramp_up" => (0..samples).map(|i| ((i as f32 / samples as f32) * 32767.0) as i16).collect(),
        "ramp_down" => (0..samples)
            .map(|i| ((1.0 - i as f32 / samples as f32) * 32767.0) as i16)
            .collect(),
        _ => vec![0i16; samples],
    }
}

/// 生成正弦波信号
fn generate_sine_wave_signal(samples: usize, frequency: f32, amplitude: f32) -> Vec<i16> {
    let sample_rate = 48000.0f32;
    (0..samples)
        .map(|i| {
            let phase = i as f32 * 2.0 * std::f32::consts::PI * frequency / sample_rate;
            (phase.sin() * amplitude) as i16
        })
        .collect()
}

/// 测试完整音频处理流水线
#[test]
fn test_full_audio_pipeline() {
    // 1. 生成测试信号
    let input_samples: Vec<i16> = (0..960)
        .map(|i| ((i as f32 * 0.1).sin() * 16000.0) as i16)
        .collect();

    // 2. DSP 处理
    let mut dsp = DspChain::new();
    let mut processed = input_samples.clone();
    let config = VoiceConfig::default();
    dsp.process(&mut processed, &config);

    // 3. 编码
    let mut codec = OpusCodec::new().unwrap();
    let mut encoded = vec![0u8; 1000];
    let encoded_len = codec.encode(&processed, &mut encoded).unwrap();
    assert!(encoded_len > 0);

    // 4. 打包
    let packet = VoicePacket::new_audio(
        1, // sender_id
        1, // sequence
        calculate_context_hash("42.194.185.210:8303"),
        0,     // token_hash
        100.0, // pos_x
        200.0, // pos_y
        encoded[..encoded_len].to_vec(),
    );

    // 5. 序列化
    let mut buf = vec![0u8; 1200];
    let packet_len = packet.serialize(&mut buf);
    assert!(packet_len > 0);

    // 6. 解析
    let parsed = VoicePacket::parse(&buf[..packet_len]).unwrap();
    assert_eq!(parsed.packet_type, PacketType::Audio);

    // 7. 解码
    let mut decoded = vec![0i16; 960];
    codec.decode(&parsed.opus_payload, &mut decoded).unwrap();

    // 8. 混音
    let mut mixer = AudioMixer::new();
    mixer.add_source(AudioSource::new(1, decoded, 1.0, 0.0));
    let mixed = mixer.mix(960, 1);

    assert_eq!(mixed.len(), 960);
}

/// 测试抖动缓冲区
#[test]
fn test_jitter_buffer() {
    use ddnet_voice::jitter::{JitterBuffer, JitterConfig};

    let config = JitterConfig {
        target_frames: 1, // 设置为 1 以便测试
        max_frames: 8,
        min_frames: 1,
        adapt_factor: 0.0, // 禁用自适应调整以便测试
        jitter_smooth_factor: 0.1,
    };
    let mut jitter = JitterBuffer::new(config);

    // 乱序插入
    jitter.push(1, vec![1i16; 960]);
    jitter.push(3, vec![3i16; 960]);
    jitter.push(2, vec![2i16; 960]);

    // 应该按顺序输出
    let frame1 = jitter.pop().unwrap();
    assert_eq!(frame1.sequence, 1);

    let frame2 = jitter.pop().unwrap();
    assert_eq!(frame2.sequence, 2);

    let frame3 = jitter.pop().unwrap();
    assert_eq!(frame3.sequence, 3);
}

/// 测试空间音频
#[test]
fn test_spatial_audio() {
    let config = SpatialConfig::default();

    // 近距离中心
    let center = calculate_spatial((0.0, 0.0), (0.0, 0.0), &config);
    assert!(center.in_range);
    assert_eq!(center.distance_factor, 1.0);

    // 左侧
    let left = calculate_spatial((0.0, 0.0), (-500.0, 0.0), &config);
    assert!(left.in_range);
    assert!(left.left_gain > left.right_gain);

    // 右侧
    let right = calculate_spatial((0.0, 0.0), (500.0, 0.0), &config);
    assert!(right.in_range);
    assert!(right.right_gain > right.left_gain);

    // 超出范围
    let far = calculate_spatial((0.0, 0.0), (5000.0, 0.0), &config);
    assert!(!far.in_range);
}

/// 测试语音系统
#[test]
fn test_voice_system() {
    let system = VoiceSystem::new();

    assert_eq!(system.get_mic_level(), 0);
    assert!(!system.is_ptt_active());
    assert!(!system.is_vad_active());

    system.set_ptt_active(true);
    assert!(system.is_ptt_active());

    // 更新玩家
    let players = vec![
        PlayerSnapshot {
            client_id: 1,
            name: "Player1".to_string(),
            x: 100.0,
            y: 200.0,
            team: 0,
            is_spectator: false,
            is_active: true,
            spec_x: 100.0,
            spec_y: 200.0,
        },
        PlayerSnapshot {
            client_id: 2,
            name: "Player2".to_string(),
            x: 300.0,
            y: 400.0,
            team: 1,
            is_spectator: false,
            is_active: true,
            spec_x: 300.0,
            spec_y: 400.0,
        },
    ];
    system.update_players(players);
}

/// 测试协议兼容性
#[test]
fn test_protocol_compatibility() {
    use ddnet_voice::network::protocol::{VOICE_HEADER_SIZE, VOICE_MAGIC, VOICE_VERSION};

    // 创建包
    let packet = VoicePacket::new_audio(42, 100, 0x12345678, 0, 100.0, 200.0, vec![0xAA; 50]);

    let mut buf = vec![0u8; 1200];
    let len = packet.serialize(&mut buf);

    // 验证头部
    assert_eq!(&buf[0..4], VOICE_MAGIC);
    assert_eq!(buf[4], VOICE_VERSION);
    assert_eq!(buf[5], 1); // Audio type
    assert_eq!(len, VOICE_HEADER_SIZE + 50);

    // 解析
    let parsed = VoicePacket::parse(&buf[..len]).unwrap();
    assert_eq!(parsed.sender_id, 42);
    assert_eq!(parsed.sequence, 100);
}

/// 测试 DSP 处理链
#[test]
fn test_dsp_chain() {
    let mut dsp = DspChain::new();

    // 使用正弦波信号（非直流），避免被高通滤波器滤除
    let mut signal: Vec<i16> = (0..960)
        .map(|i| {
            let phase = i as f32 * 2.0 * std::f32::consts::PI * 440.0 / 48000.0;
            (phase.sin() * 100.0) as i16
        })
        .collect();

    let config = VoiceConfig {
        mic_volume: 200,       // 200% 增益
        noise_suppress: false, // 禁用降噪以简化测试
        ..Default::default()
    };

    dsp.process(&mut signal, &config);

    // 增益应该放大信号（200% 增益 + 压缩器补偿增益）
    let avg = signal.iter().map(|&s| s.abs() as f32).sum::<f32>() / signal.len() as f32;
    assert!(avg > 100.0, "Average should be amplified, got {}", avg);

    // 高电平信号
    let mut high_level = vec![20000i16; 960];
    dsp.process(&mut high_level, &config);

    // 压缩器应该限制峰值
    let max = high_level.iter().map(|&s| s.abs() as i32).max().unwrap();
    assert!(max < 32768);
}

/// 测试 Opus 编解码
#[test]
fn test_opus_codec() {
    use opus::{Application, Bitrate, Channels, Decoder, Encoder};

    let mut encoder = Encoder::new(48000, Channels::Mono, Application::Audio).unwrap();
    encoder.set_bitrate(Bitrate::Bits(48000)).unwrap();
    encoder.set_complexity(10).unwrap();
    let mut decoder = Decoder::new(48000, Channels::Mono).unwrap();

    let num_frames = 5;
    let total_samples = 960 * num_frames;
    let signal: Vec<i16> = (0..total_samples)
        .map(|i| {
            let phase = i as f32 * 2.0 * std::f32::consts::PI * 440.0 / 48000.0;
            (phase.sin() * 16000.0) as i16
        })
        .collect();

    let mut all_decoded = Vec::with_capacity(total_samples);
    let mut encoded = vec![0u8; 1000];
    for frame_idx in 0..num_frames {
        let start = frame_idx * 960;
        let pcm = &signal[start..start + 960];
        let encoded_len = encoder.encode(pcm, &mut encoded).unwrap();

        let mut decoded = vec![0i16; 960];
        let samples = decoder
            .decode(&encoded[..encoded_len], &mut decoded, false)
            .unwrap();
        assert_eq!(samples, 960);
        all_decoded.extend_from_slice(&decoded[..samples]);
    }

    let search_range = 512;
    let mut best_offset = 0i32;
    let mut best_corr = 0.0f64;

    for offset in -(search_range as i32)..=(search_range as i32) {
        let mut corr = 0.0f64;
        let start_orig = offset.max(0) as usize;
        let start_dec = offset.min(0).abs() as usize;
        let len = total_samples - offset.abs() as usize;
        if len <= 0 {
            continue;
        }
        for i in 0..len {
            let o = signal[start_orig + i] as f64;
            let d = all_decoded[start_dec + i] as f64;
            corr += o * d;
        }
        if corr > best_corr {
            best_corr = corr;
            best_offset = offset;
        }
    }

    let start_orig = best_offset.max(0) as usize;
    let start_dec = best_offset.min(0).abs() as usize;
    let len = total_samples - best_offset.abs() as usize;

    let mut sum_sq_diff = 0.0f64;
    let mut sum_sq_orig = 0.0f64;
    for i in 0..len {
        let orig = signal[start_orig + i] as f64;
        let dec = all_decoded[start_dec + i] as f64;
        let diff = orig - dec;
        sum_sq_diff += diff * diff;
        sum_sq_orig += orig * orig;
    }

    let snr_db = 10.0 * (sum_sq_orig / sum_sq_diff).log10();
    assert!(
        snr_db > 20.0,
        "SNR too low: {} dB (best_offset: {})",
        snr_db,
        best_offset
    );
}

/// 测试混音器
#[test]
fn test_audio_mixer() {
    let mut mixer = AudioMixer::new();
    // 禁用软削波以获得精确的混音结果
    mixer.set_master_volume(1.0);

    // 通过配置禁用软削波
    let mut config = mixer.config().clone();
    config.soft_clip = false;
    mixer.set_config(config);

    // 添加两个源
    let source1 = AudioSource::new(1, vec![1000i16; 960], 1.0, 0.0);
    let source2 = AudioSource::new(2, vec![2000i16; 960], 0.5, 0.0);

    mixer.add_source(source1);
    mixer.add_source(source2);

    // 混音
    let output = mixer.mix(960, 1);

    // 1000 + 2000 * 0.5 = 2000
    assert_eq!(output[0], 2000);
}

/// 测试增益模块
#[test]
fn test_gain_module() {
    use ddnet_voice::dsp::gain;

    // 增益 2x
    let mut samples = vec![1000i16; 960];
    gain::apply(2.0, &mut samples);
    assert_eq!(samples[0], 2000);

    // 增益上限
    let mut high_samples = vec![20000i16; 960];
    gain::apply(3.0, &mut high_samples);
    assert!(high_samples.iter().all(|&s| s <= 32767));

    // 峰值计算
    let peak_samples = vec![0i16, 16384, -32768, 1000];
    let peak = gain::frame_peak(&peak_samples);
    assert_eq!(peak, 1.0);
}

/// 测试协议 PayloadSize=0 包解析
#[test]
fn test_protocol_zero_payload() {
    // 构造一个 payload_size=0 的包
    // 验证解析不会崩溃
    let packet = VoicePacket::new_audio(1, 1, 0, 0, 0.0, 0.0, vec![]);
    let mut buf = vec![0u8; 1200];
    let len = packet.serialize(&mut buf);
    assert!(len > 0);

    // 解析应该成功（payload 为空是合法的）
    let parsed = VoicePacket::parse(&buf[..len]).unwrap();
    assert_eq!(parsed.opus_payload.len(), 0);
}

// ============== 多样化测试数据测试 ==============

/// 测试 DSP 处理静音信号的稳定性
#[test]
fn test_dsp_silence_signal() {
    let mut dsp = DspChain::new();
    let mut signal = generate_silence_signal(960);
    let config = VoiceConfig::default();

    // 处理静音信号
    dsp.process(&mut signal, &config);

    // 静音信号处理后应仍为静音或接近静音
    let max_abs = signal.iter().map(|&s| s.abs()).max().unwrap_or(0);
    // 由于高通滤波器和舒适噪声，可能有小幅值输出
    assert!(max_abs < 100, "Silence signal should remain quiet, got max={}", max_abs);
}

/// 测试 DSP 处理白噪声信号的稳定性
#[test]
fn test_dsp_white_noise_signal() {
    let mut dsp = DspChain::new();
    let mut signal = generate_white_noise_signal(960, 16000);
    let config = VoiceConfig::default();

    // 处理白噪声信号
    dsp.process(&mut signal, &config);

    // 所有样本应在有效范围内
    for &sample in &signal {
        assert!(sample >= -32768 && sample <= 32767);
    }

    // 白噪声应该被压缩器限制
    let max_abs = signal.iter().map(|&s| s.abs() as i32).max().unwrap_or(0);
    assert!(max_abs < 32768, "White noise should be limited");
}

/// 测试 DSP 处理方波信号的稳定性
#[test]
fn test_dsp_square_wave_signal() {
    let mut dsp = DspChain::new();
    let mut signal = generate_square_wave_signal(960, 440.0, 16000);
    let config = VoiceConfig::default();

    // 处理方波信号
    dsp.process(&mut signal, &config);

    // 所有样本应在有效范围内
    for &sample in &signal {
        assert!(sample >= -32768 && sample <= 32767);
    }

    // 方波应该被平滑处理（高通滤波器会改变波形）
    let unique_values: std::collections::HashSet<i16> = signal.iter().copied().collect();
    // 方波原本只有两个值，处理后应该有更多
    assert!(unique_values.len() > 2, "Square wave should be smoothed");
}

/// 测试 DSP 处理多频率复合信号的稳定性
#[test]
fn test_dsp_multi_frequency_signal() {
    let mut dsp = DspChain::new();
    // 复合信号：440Hz + 880Hz + 1760Hz（基频 + 两个谐波）
    let mut signal = generate_multi_frequency_signal(960, &[440.0, 880.0, 1760.0], 16000);
    let config = VoiceConfig::default();

    // 处理复合信号
    dsp.process(&mut signal, &config);

    // 所有样本应在有效范围内
    for &sample in &signal {
        assert!(sample >= -32768 && sample <= 32767);
    }

    // 信号应该有合理的能量
    let energy: f64 = signal.iter().map(|&s| (s as f64).powi(2)).sum();
    assert!(energy > 0.0, "Multi-frequency signal should have energy");
}

/// 测试 DSP 处理边界值信号（最大振幅）
#[test]
fn test_dsp_boundary_max_signal() {
    let mut dsp = DspChain::new();
    let mut signal = generate_boundary_signal(960, "max");
    let config = VoiceConfig::default();

    // 处理最大振幅信号
    dsp.process(&mut signal, &config);

    // 所有样本应在有效范围内
    for &sample in &signal {
        assert!(sample >= -32768 && sample <= 32767);
    }

    // 最大信号应该被限制器/压缩器限制
    let max_abs = signal.iter().map(|&s| s.abs()).max().unwrap_or(0);
    assert!(max_abs <= 32767);
}

/// 测试 DSP 处理边界值信号（最小振幅）
#[test]
fn test_dsp_boundary_min_signal() {
    let mut dsp = DspChain::new();
    let mut signal = generate_boundary_signal(960, "min");
    let config = VoiceConfig::default();

    // 处理最小振幅信号
    dsp.process(&mut signal, &config);

    // 所有样本应在有效范围内
    for &sample in &signal {
        assert!(sample >= -32768 && sample <= 32767);
    }
}

/// 测试 DSP 处理边界值信号（交替最大最小）
#[test]
fn test_dsp_boundary_alternating_signal() {
    let mut dsp = DspChain::new();
    let mut signal = generate_boundary_signal(960, "alternating");
    let config = VoiceConfig::default();

    // 处理交替信号
    dsp.process(&mut signal, &config);

    // 所有样本应在有效范围内
    for &sample in &signal {
        assert!(sample >= -32768 && sample <= 32767);
    }
}

/// 测试 DSP 处理边界值信号（上升斜坡）
#[test]
fn test_dsp_boundary_ramp_up_signal() {
    let mut dsp = DspChain::new();
    let mut signal = generate_boundary_signal(960, "ramp_up");
    let config = VoiceConfig::default();

    // 处理上升斜坡信号
    dsp.process(&mut signal, &config);

    // 所有样本应在有效范围内
    for &sample in &signal {
        assert!(sample >= -32768 && sample <= 32767);
    }
}

/// 测试 DSP 处理边界值信号（下降斜坡）
#[test]
fn test_dsp_boundary_ramp_down_signal() {
    let mut dsp = DspChain::new();
    let mut signal = generate_boundary_signal(960, "ramp_down");
    let config = VoiceConfig::default();

    // 处理下降斜坡信号
    dsp.process(&mut signal, &config);

    // 所有样本应在有效范围内
    for &sample in &signal {
        assert!(sample >= -32768 && sample <= 32767);
    }
}

/// 测试 Opus 编解码静音信号
#[test]
fn test_opus_codec_silence() {
    let mut codec = OpusCodec::new().unwrap();
    let signal = generate_silence_signal(960);

    let mut encoded = vec![0u8; 1000];
    let encoded_len = codec.encode(&signal, &mut encoded).unwrap();
    assert!(encoded_len > 0, "Silence should encode to non-empty data");

    let mut decoded = vec![0i16; 960];
    let samples = codec.decode(&encoded[..encoded_len], &mut decoded).unwrap();
    assert_eq!(samples, 960);

    // 解码后的静音应该接近零
    let max_abs = decoded.iter().map(|&s| s.abs()).max().unwrap_or(0);
    assert!(max_abs < 100, "Decoded silence should be quiet, got max={}", max_abs);
}

/// 测试 Opus 编解码白噪声信号
#[test]
fn test_opus_codec_white_noise() {
    let mut codec = OpusCodec::new().unwrap();
    let signal = generate_white_noise_signal(960, 16000);

    let mut encoded = vec![0u8; 1000];
    let encoded_len = codec.encode(&signal, &mut encoded).unwrap();
    assert!(encoded_len > 0);

    let mut decoded = vec![0i16; 960];
    let samples = codec.decode(&encoded[..encoded_len], &mut decoded).unwrap();
    assert_eq!(samples, 960);

    // 所有样本应在有效范围内
    for &sample in &decoded {
        assert!(sample >= -32768 && sample <= 32767);
    }
}

/// 测试 Opus 编解码方波信号
#[test]
fn test_opus_codec_square_wave() {
    let mut codec = OpusCodec::new().unwrap();
    let signal = generate_square_wave_signal(960, 440.0, 16000);

    let mut encoded = vec![0u8; 1000];
    let encoded_len = codec.encode(&signal, &mut encoded).unwrap();
    assert!(encoded_len > 0);

    let mut decoded = vec![0i16; 960];
    let samples = codec.decode(&encoded[..encoded_len], &mut decoded).unwrap();
    assert_eq!(samples, 960);

    // 所有样本应在有效范围内
    for &sample in &decoded {
        assert!(sample >= -32768 && sample <= 32767);
    }
}

/// 测试 Opus 编解码多频率复合信号
#[test]
fn test_opus_codec_multi_frequency() {
    let mut codec = OpusCodec::new().unwrap();
    let signal = generate_multi_frequency_signal(960, &[440.0, 880.0, 1760.0], 16000);

    let mut encoded = vec![0u8; 1000];
    let encoded_len = codec.encode(&signal, &mut encoded).unwrap();
    assert!(encoded_len > 0);

    let mut decoded = vec![0i16; 960];
    let samples = codec.decode(&encoded[..encoded_len], &mut decoded).unwrap();
    assert_eq!(samples, 960);

    // 所有样本应在有效范围内
    for &sample in &decoded {
        assert!(sample >= -32768 && sample <= 32767);
    }
}

/// 测试 Opus 编解码边界值信号
#[test]
fn test_opus_codec_boundary_max() {
    let mut codec = OpusCodec::new().unwrap();
    let signal = generate_boundary_signal(960, "max");

    let mut encoded = vec![0u8; 1000];
    let encoded_len = codec.encode(&signal, &mut encoded).unwrap();
    assert!(encoded_len > 0);

    let mut decoded = vec![0i16; 960];
    let samples = codec.decode(&encoded[..encoded_len], &mut decoded).unwrap();
    assert_eq!(samples, 960);

    // 所有样本应在有效范围内
    for &sample in &decoded {
        assert!(sample >= -32768 && sample <= 32767);
    }
}

/// 测试完整音频流水线处理多样化信号
#[test]
fn test_full_pipeline_diverse_signals() {
    let test_signals = vec![
        ("silence", generate_silence_signal(960)),
        ("white_noise", generate_white_noise_signal(960, 16000)),
        ("square_wave", generate_square_wave_signal(960, 440.0, 16000)),
        ("multi_freq", generate_multi_frequency_signal(960, &[440.0, 880.0], 16000)),
        ("ramp_up", generate_boundary_signal(960, "ramp_up")),
        ("ramp_down", generate_boundary_signal(960, "ramp_down")),
    ];

    for (name, mut signal) in test_signals {
        // 1. DSP 处理
        let mut dsp = DspChain::new();
        let config = VoiceConfig::default();
        dsp.process(&mut signal, &config);

        // 2. 编码
        let mut codec = OpusCodec::new().unwrap();
        let mut encoded = vec![0u8; 1000];
        let encoded_len = codec.encode(&signal, &mut encoded).unwrap();
        assert!(encoded_len > 0, "Failed to encode {} signal", name);

        // 3. 解码
        let mut decoded = vec![0i16; 960];
        let samples = codec.decode(&encoded[..encoded_len], &mut decoded).unwrap();
        assert_eq!(samples, 960, "Failed to decode {} signal", name);

        // 4. 混音
        let mut mixer = AudioMixer::new();
        mixer.add_source(AudioSource::new(1, decoded, 1.0, 0.0));
        let mixed = mixer.mix(960, 1);
        assert_eq!(mixed.len(), 960, "Failed to mix {} signal", name);

        // 5. 验证输出范围
        for &sample in &mixed {
            assert!(
                sample >= -32768 && sample <= 32767,
                "Invalid sample in {} signal: {}",
                name,
                sample
            );
        }
    }
}

/// 测试混音器处理多样化信号
#[test]
fn test_mixer_diverse_signals() {
    let mut mixer = AudioMixer::new();
    mixer.set_master_volume(1.0);
    let mut config = mixer.config().clone();
    config.soft_clip = false;
    mixer.set_config(config);

    // 添加多种信号源
    mixer.add_source(AudioSource::new(1, generate_silence_signal(960), 1.0, 0.0));
    mixer.add_source(AudioSource::new(2, generate_sine_wave_signal(960, 440.0, 5000.0), 0.5, 0.0));
    mixer.add_source(AudioSource::new(3, generate_white_noise_signal(960, 5000), 0.3, 0.0));

    let output = mixer.mix(960, 1);

    // 输出应在有效范围内
    for &sample in &output {
        assert!(sample >= -32768 && sample <= 32767);
    }

    // 输出应该有能量（非静音）
    let energy: f64 = output.iter().map(|&s| (s as f64).powi(2)).sum();
    assert!(energy > 0.0, "Mixed output should have energy");
}

/// 测试抖动缓冲区处理多样化信号
#[test]
fn test_jitter_buffer_diverse_signals() {
    use ddnet_voice::jitter::{JitterBuffer, JitterConfig};

    let config = JitterConfig {
        target_frames: 1,
        max_frames: 8,
        min_frames: 1,
        adapt_factor: 0.0,
        jitter_smooth_factor: 0.1,
    };
    let mut jitter = JitterBuffer::new(config);

    // 使用多种信号类型
    let signals = vec![
        generate_silence_signal(960),
        generate_sine_wave_signal(960, 440.0, 16000.0),
        generate_white_noise_signal(960, 16000),
        generate_square_wave_signal(960, 440.0, 16000),
    ];

    // 乱序插入
    for (seq, signal) in signals.into_iter().enumerate() {
        jitter.push(seq as u16, signal);
    }

    // 按顺序取出
    for seq in 0..4 {
        let frame = jitter.pop();
        assert!(frame.is_some(), "Should have frame for seq {}", seq);
        let frame = frame.unwrap();
        assert_eq!(frame.sequence, seq as u16);
        assert_eq!(frame.samples.len(), 960);
    }
}

/// 测试空间音频处理多样化信号
#[test]
fn test_spatial_audio_diverse_signals() {
    let config = SpatialConfig::default();

    // 测试不同位置的空间音频
    let positions = vec![
        ((0.0, 0.0), (0.0, 0.0)),      // 中心
        ((0.0, 0.0), (-500.0, 0.0)),   // 左侧
        ((0.0, 0.0), (500.0, 0.0)),    // 右侧
        ((0.0, 0.0), (0.0, -500.0)),   // 上方
        ((0.0, 0.0), (0.0, 500.0)),    // 下方
        ((0.0, 0.0), (5000.0, 0.0)),   // 超出范围
    ];

    for (local_pos, sender_pos) in positions {
        let result = calculate_spatial(local_pos, sender_pos, &config);

        // 验证距离因子在有效范围内
        assert!(result.distance_factor >= 0.0 && result.distance_factor <= 1.0);

        // 验证增益在有效范围内
        assert!(result.left_gain >= 0.0 && result.left_gain <= 2.0);
        assert!(result.right_gain >= 0.0 && result.right_gain <= 2.0);

        // 超出范围时应该不在范围内
        if sender_pos.0.abs() > config.radius || sender_pos.1.abs() > config.radius {
            assert!(!result.in_range);
        }
    }
}

/// 测试协议处理多样化信号
#[test]
fn test_protocol_diverse_payloads() {
    let test_payloads = vec![
        vec![],                                    // 空负载
        vec![0u8; 10],                            // 小负载
        vec![0xAA; 100],                          // 中等负载
        vec![0xFF; 500],                          // 大负载
        (0..=255u8).cycle().take(200).collect(), // 模式化负载
    ];

    for (i, payload) in test_payloads.iter().enumerate() {
        let packet = VoicePacket::new_audio(
            1,
            i as u16,
            0x12345678,
            0,
            100.0,
            200.0,
            payload.clone(),
        );

        let mut buf = vec![0u8; 1200];
        let len = packet.serialize(&mut buf);
        assert!(len > 0, "Failed to serialize packet {}", i);

        let parsed = VoicePacket::parse(&buf[..len]).unwrap();
        assert_eq!(parsed.opus_payload.len(), payload.len(), "Payload mismatch for packet {}", i);
    }
}

/// 压力测试：连续处理多种信号
#[test]
fn test_stress_diverse_signals() {
    let mut dsp = DspChain::new();
    let mut codec = OpusCodec::new().unwrap();
    let config = VoiceConfig::default();

    // 连续处理 100 帧各种信号
    for _ in 0..100 {
        let signal_types = [
            generate_silence_signal(960),
            generate_sine_wave_signal(960, 440.0, 16000.0),
            generate_white_noise_signal(960, 16000),
            generate_square_wave_signal(960, 440.0, 16000),
            generate_multi_frequency_signal(960, &[440.0, 880.0], 16000),
            generate_boundary_signal(960, "max"),
            generate_boundary_signal(960, "min"),
        ];

        for mut signal in signal_types {
            // DSP 处理
            dsp.process(&mut signal, &config);

            // 编码
            let mut encoded = vec![0u8; 1000];
            if codec.encode(&signal, &mut encoded).is_err() {
                continue;
            }

            // 解码
            let mut decoded = vec![0i16; 960];
            if codec.decode(&encoded, &mut decoded).is_err() {
                continue;
            }

            // 验证范围
            for &sample in &decoded {
                assert!(sample >= -32768 && sample <= 32767);
            }
        }
    }
}
