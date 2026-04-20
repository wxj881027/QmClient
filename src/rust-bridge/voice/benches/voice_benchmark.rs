//! 语音模块性能基准测试
//!
//! 性能阈值（热路径要求）：
//! - DSP 处理链: < 1ms
//! - AEC 处理: < 0.1ms
//! - Opus 编解码: < 5ms
//! - 混音操作: < 0.1ms

use criterion::{black_box, criterion_group, criterion_main, BenchmarkId, Criterion, Throughput};
use ddnet_voice::{
    audio::mixer::{AudioMixer, AudioSource},
    codec::OpusCodec,
    dsp::{aec::AcousticEchoCanceller, compressor, gain, hpf, DspChain},
    jitter::JitterBuffer,
    network::protocol::{PacketType, VoicePacket},
    spatial::{calculate_spatial, SpatialConfig},
};

/// 性能阈值常量（微秒）
mod perf_thresholds {
    /// DSP 处理链阈值: 1ms = 1000us
    pub const DSP_CHAIN_US: u64 = 1000;
    /// AEC 处理阈值: 0.1ms = 100us
    pub const AEC_US: u64 = 100;
    /// Opus 编码阈值: 5ms = 5000us
    pub const OPUS_ENCODE_US: u64 = 5000;
    /// Opus 解码阈值: 5ms = 5000us
    pub const OPUS_DECODE_US: u64 = 5000;
    /// 混音阈值: 0.1ms = 100us
    pub const MIXER_US: u64 = 100;
}

fn dsp_benchmark(c: &mut Criterion) {
    let mut group = c.benchmark_group("dsp");
    group.throughput(Throughput::Elements(960)); // 每帧 960 采样

    // 增益测试
    let samples: Vec<i16> = (0..960).map(|i| (i % 100) as i16).collect();
    group.bench_function("gain", |b| {
        let mut buf = samples.clone();
        b.iter(|| {
            gain::apply(black_box(2.0), black_box(&mut buf));
        })
    });

    // 高通滤波测试
    let samples: Vec<i16> = (0..960).map(|i| (i % 100) as i16).collect();
    let mut hpf_state = hpf::HpfState::default();
    group.bench_function("hpf", |b| {
        let mut buf = samples.clone();
        b.iter(|| {
            hpf::apply(black_box(&mut buf), black_box(&mut hpf_state));
        })
    });

    // 压缩器测试
    let samples: Vec<i16> = (0..960).map(|i| (i % 100) as i16).collect();
    let mut comp_state = compressor::CompressorState::default();
    let comp_config = compressor::CompressorConfig::default();
    group.bench_function("compressor", |b| {
        let mut buf = samples.clone();
        b.iter(|| {
            compressor::apply(
                black_box(&mut buf),
                black_box(&mut comp_state),
                black_box(&comp_config),
            );
        })
    });

    // 完整 DSP 链测试（热路径）
    let samples: Vec<i16> = (0..960).map(|i| (i % 100) as i16).collect();
    let mut dsp = DspChain::new();
    let config = ddnet_voice::VoiceConfig::default();
    group.bench_function("dsp_chain", |b| {
        let mut buf = samples.clone();
        b.iter(|| {
            dsp.process(black_box(&mut buf), black_box(&config));
        })
    });

    group.finish();
}

/// AEC (回声消除) 性能基准测试
fn aec_benchmark(c: &mut Criterion) {
    let mut group = c.benchmark_group("aec");
    group.throughput(Throughput::Elements(960));

    let mut aec = AcousticEchoCanceller::new();
    let frame_size = 960;

    // 生成测试信号
    let near_end: Vec<i16> = (0..frame_size)
        .map(|i| ((i as f32 * 0.1).sin() * 1000.0) as i16)
        .collect();
    let far_end: Vec<i16> = (0..frame_size)
        .map(|i| ((i as f32 * 0.05).sin() * 800.0) as i16)
        .collect();
    let mut output = vec![0i16; frame_size];

    // AEC 处理测试（热路径）
    group.bench_function("process_frame", |b| {
        b.iter(|| {
            aec.process_frame(
                black_box(&near_end),
                black_box(&far_end),
                black_box(&mut output),
            );
        })
    });

    // AEC 收敛测试（多帧处理）
    group.bench_function("convergence_100_frames", |b| {
        b.iter(|| {
            let mut aec = AcousticEchoCanceller::new();
            for _ in 0..100 {
                aec.process_frame(&near_end, &far_end, &mut output);
            }
        })
    });

    group.finish();
}

fn codec_benchmark(c: &mut Criterion) {
    let mut group = c.benchmark_group("codec");
    group.throughput(Throughput::Elements(960));

    let samples: Vec<i16> = (0..960)
        .map(|i| ((i as f32 * 0.1).sin() * 16000.0) as i16)
        .collect();
    let mut codec = OpusCodec::new().unwrap();

    // 编码测试（热路径）
    group.bench_function("encode", |b| {
        let mut output = vec![0u8; 1000];
        b.iter(|| {
            codec
                .encode(black_box(&samples), black_box(&mut output))
                .unwrap()
        })
    });

    // 解码测试（热路径）
    let mut encoded = vec![0u8; 1000];
    let len = codec.encode(&samples, &mut encoded).unwrap();
    group.bench_function("decode", |b| {
        let mut output = vec![0i16; 960];
        b.iter(|| {
            codec
                .decode(black_box(&encoded[..len]), black_box(&mut output))
                .unwrap()
        })
    });

    // PLC 解码测试（丢包隐藏）
    group.bench_function("decode_plc", |b| {
        let mut output = vec![0i16; 960];
        b.iter(|| codec.decode_plc(black_box(&mut output)).unwrap())
    });

    // FEC 解码测试
    codec.set_fec(true).unwrap();
    let mut enc1 = vec![0u8; 1000];
    let mut enc2 = vec![0u8; 1000];
    let _ = codec.encode(&samples, &mut enc1).unwrap();
    let _ = codec.encode(&samples, &mut enc2).unwrap();
    group.bench_function("decode_fec", |b| {
        let mut output = vec![0i16; 960];
        b.iter(|| {
            codec
                .decode_fec(black_box(&enc2), black_box(&mut output))
                .unwrap()
        })
    });

    group.finish();
}

fn protocol_benchmark(c: &mut Criterion) {
    let mut group = c.benchmark_group("protocol");

    let packet = VoicePacket::new_audio(1, 1, 0x12345678, 0, 100.0, 200.0, vec![0xAA; 50]);

    // 序列化测试
    group.bench_function("serialize", |b| {
        let mut buf = vec![0u8; 1200];
        b.iter(|| packet.serialize(black_box(&mut buf)))
    });

    // 解析测试
    let mut buf = vec![0u8; 1200];
    let len = packet.serialize(&mut buf);
    group.bench_function("parse", |b| {
        b.iter(|| VoicePacket::parse(black_box(&buf[..len])).unwrap())
    });

    group.finish();
}

fn jitter_benchmark(c: &mut Criterion) {
    let mut group = c.benchmark_group("jitter");

    let mut jitter = JitterBuffer::default();

    group.bench_function("push_pop", |b| {
        let mut seq = 0u16;
        let samples = vec![0i16; 960];
        b.iter(|| {
            jitter.push(black_box(seq), black_box(samples.clone()));
            seq = seq.wrapping_add(1);
            if seq % 3 == 0 {
                jitter.pop();
            }
        })
    });

    group.finish();
}

fn spatial_benchmark(c: &mut Criterion) {
    let mut group = c.benchmark_group("spatial");

    let config = SpatialConfig::default();

    group.bench_function("calculate_spatial", |b| {
        b.iter(|| {
            calculate_spatial(
                black_box((0.0, 0.0)),
                black_box((500.0, 500.0)),
                black_box(&config),
            )
        })
    });

    group.finish();
}

fn mixer_benchmark(c: &mut Criterion) {
    let mut group = c.benchmark_group("mixer");
    group.throughput(Throughput::Elements(960));

    for num_sources in [1, 2, 4, 8].iter() {
        group.bench_with_input(
            BenchmarkId::new("mix", num_sources),
            num_sources,
            |b, &num_sources| {
                let mut mixer = AudioMixer::new();

                for i in 0..num_sources {
                    mixer.add_source(AudioSource::new(i as u16, vec![1000i16; 960], 1.0, 0.0));
                }

                b.iter(|| mixer.mix(black_box(960), black_box(1)));
            },
        );
    }

    // 混音零拷贝版本测试（热路径）
    group.bench_function("mix_into_zero_copy", |b| {
        let mut mixer = AudioMixer::new();
        for i in 0..4 {
            mixer.add_source(AudioSource::new(i as u16, vec![1000i16; 960], 1.0, 0.0));
        }
        let mut output = [0i16; 960];
        b.iter(|| {
            mixer.mix_into(black_box(&mut output), black_box(960), black_box(1));
        })
    });

    // 立体声混音测试
    group.bench_function("mix_stereo_4_sources", |b| {
        let mut mixer = AudioMixer::new();
        for i in 0..4 {
            mixer.add_source(AudioSource::new(i as u16, vec![1000i16; 960], 1.0, 0.0));
        }
        b.iter(|| mixer.mix(black_box(960), black_box(2)));
    });

    group.finish();
}

/// 性能回归测试
///
/// 验证关键热路径的处理时间不超过阈值。
/// 如果超过阈值，测试将失败并报告性能退化。
fn performance_regression_test(c: &mut Criterion) {
    let mut group = c.benchmark_group("performance_regression");
    group.sample_size(100); // 使用更多样本以获得更准确的结果

    // DSP 处理链性能断言
    let samples: Vec<i16> = (0..960).map(|i| (i % 100) as i16).collect();
    let mut dsp = DspChain::new();
    let config = ddnet_voice::VoiceConfig::default();

    group.bench_function("dsp_chain_threshold", |b| {
        b.iter_custom(|iters| {
            let mut buf = samples.clone();
            let start = std::time::Instant::now();
            for _ in 0..iters {
                dsp.process(&mut buf, &config);
            }
            let elapsed = start.elapsed();
            let avg_us = elapsed.as_micros() as f64 / iters as f64;

            // 性能断言：DSP 处理链应 < 1ms
            if avg_us > perf_thresholds::DSP_CHAIN_US as f64 {
                panic!(
                    "DSP chain performance regression: {:.2}us > {}us threshold",
                    avg_us,
                    perf_thresholds::DSP_CHAIN_US
                );
            }
            elapsed
        })
    });

    // AEC 处理性能断言
    let mut aec = AcousticEchoCanceller::new();
    let near_end: Vec<i16> = (0..960)
        .map(|i| ((i as f32 * 0.1).sin() * 1000.0) as i16)
        .collect();
    let far_end: Vec<i16> = (0..960)
        .map(|i| ((i as f32 * 0.05).sin() * 800.0) as i16)
        .collect();
    let mut output = vec![0i16; 960];

    group.bench_function("aec_threshold", |b| {
        b.iter_custom(|iters| {
            let start = std::time::Instant::now();
            for _ in 0..iters {
                aec.process_frame(&near_end, &far_end, &mut output);
            }
            let elapsed = start.elapsed();
            let avg_us = elapsed.as_micros() as f64 / iters as f64;

            // 性能断言：AEC 处理应 < 0.1ms
            if avg_us > perf_thresholds::AEC_US as f64 {
                panic!(
                    "AEC performance regression: {:.2}us > {}us threshold",
                    avg_us,
                    perf_thresholds::AEC_US
                );
            }
            elapsed
        })
    });

    // Opus 编码性能断言
    let mut codec = OpusCodec::new().unwrap();
    let samples: Vec<i16> = (0..960)
        .map(|i| ((i as f32 * 0.1).sin() * 16000.0) as i16)
        .collect();
    let mut encoded = vec![0u8; 1000];

    group.bench_function("opus_encode_threshold", |b| {
        b.iter_custom(|iters| {
            let start = std::time::Instant::now();
            for _ in 0..iters {
                codec.encode(&samples, &mut encoded).unwrap();
            }
            let elapsed = start.elapsed();
            let avg_us = elapsed.as_micros() as f64 / iters as f64;

            // 性能断言：Opus 编码应 < 5ms
            if avg_us > perf_thresholds::OPUS_ENCODE_US as f64 {
                panic!(
                    "Opus encode performance regression: {:.2}us > {}us threshold",
                    avg_us,
                    perf_thresholds::OPUS_ENCODE_US
                );
            }
            elapsed
        })
    });

    // Opus 解码性能断言
    let len = codec.encode(&samples, &mut encoded).unwrap();
    let mut decoded = vec![0i16; 960];

    group.bench_function("opus_decode_threshold", |b| {
        b.iter_custom(|iters| {
            let start = std::time::Instant::now();
            for _ in 0..iters {
                codec.decode(&encoded[..len], &mut decoded).unwrap();
            }
            let elapsed = start.elapsed();
            let avg_us = elapsed.as_micros() as f64 / iters as f64;

            // 性能断言：Opus 解码应 < 5ms
            if avg_us > perf_thresholds::OPUS_DECODE_US as f64 {
                panic!(
                    "Opus decode performance regression: {:.2}us > {}us threshold",
                    avg_us,
                    perf_thresholds::OPUS_DECODE_US
                );
            }
            elapsed
        })
    });

    // 混音性能断言
    let mut mixer = AudioMixer::new();
    for i in 0..4 {
        mixer.add_source(AudioSource::new(i as u16, vec![1000i16; 960], 1.0, 0.0));
    }

    group.bench_function("mixer_threshold", |b| {
        b.iter_custom(|iters| {
            let start = std::time::Instant::now();
            for _ in 0..iters {
                mixer.mix(960, 1);
            }
            let elapsed = start.elapsed();
            let avg_us = elapsed.as_micros() as f64 / iters as f64;

            // 性能断言：混音应 < 0.1ms
            if avg_us > perf_thresholds::MIXER_US as f64 {
                panic!(
                    "Mixer performance regression: {:.2}us > {}us threshold",
                    avg_us,
                    perf_thresholds::MIXER_US
                );
            }
            elapsed
        })
    });

    group.finish();
}

criterion_group!(
    benches,
    dsp_benchmark,
    aec_benchmark,
    codec_benchmark,
    protocol_benchmark,
    jitter_benchmark,
    spatial_benchmark,
    mixer_benchmark,
    performance_regression_test,
);
criterion_main!(benches);
