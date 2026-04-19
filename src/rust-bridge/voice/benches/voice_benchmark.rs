//! 语音模块性能基准测试

use criterion::{black_box, criterion_group, criterion_main, Criterion, BenchmarkId};
use ddnet_voice::{
    dsp::{DspChain, gain, hpf, compressor},
    codec::OpusCodec,
    network::protocol::{VoicePacket, PacketType},
    jitter::JitterBuffer,
    spatial::{calculate_spatial, SpatialConfig},
    audio::mixer::{AudioMixer, AudioSource},
};

fn dsp_benchmark(c: &mut Criterion) {
    let mut group = c.benchmark_group("dsp");

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
            compressor::apply(black_box(&mut buf), black_box(&mut comp_state), black_box(&comp_config));
        })
    });

    // 完整 DSP 链测试
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

fn codec_benchmark(c: &mut Criterion) {
    let mut group = c.benchmark_group("codec");

    let samples: Vec<i16> = (0..960)
        .map(|i| ((i as f32 * 0.1).sin() * 16000.0) as i16)
        .collect();
    let mut codec = OpusCodec::new().unwrap();

    // 编码测试
    group.bench_function("encode", |b| {
        let mut output = vec![0u8; 1000];
        b.iter(|| {
            codec.encode(black_box(&samples), black_box(&mut output)).unwrap()
        })
    });

    // 解码测试
    let mut encoded = vec![0u8; 1000];
    let len = codec.encode(&samples, &mut encoded).unwrap();
    group.bench_function("decode", |b| {
        let mut output = vec![0i16; 960];
        b.iter(|| {
            codec.decode(black_box(&encoded[..len]), black_box(&mut output)).unwrap()
        })
    });

    group.finish();
}

fn protocol_benchmark(c: &mut Criterion) {
    let mut group = c.benchmark_group("protocol");

    let packet = VoicePacket::new_audio(
        1,
        1,
        0x12345678,
        0,
        100.0,
        200.0,
        vec![0xAA; 50],
    );

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

    for num_sources in [1, 2, 4, 8].iter() {
        group.bench_with_input(
            BenchmarkId::new("mix", num_sources),
            num_sources,
            |b, &num_sources| {
                let mut mixer = AudioMixer::new();
                
                for i in 0..num_sources {
                    mixer.add_source(AudioSource::new(
                        i as u16,
                        vec![1000i16; 960],
                        1.0,
                        0.0,
                    ));
                }

                b.iter(|| {
                    mixer.mix(black_box(960), black_box(1))
                });
            },
        );
    }

    group.finish();
}

criterion_group!(
    benches,
    dsp_benchmark,
    codec_benchmark,
    protocol_benchmark,
    jitter_benchmark,
    spatial_benchmark,
    mixer_benchmark
);
criterion_main!(benches);
