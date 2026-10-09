//! QmClient 的 WS/WSS 传输；句柄只允许由 C++ 网络线程访问。

use std::ffi::{c_char, c_void, CStr};
use std::io::{self, Read, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::ptr;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError};
use std::time::{Duration, Instant};
use tungstenite::client::IntoClientRequest;
use tungstenite::http::header::{HeaderName, HeaderValue, SEC_WEBSOCKET_PROTOCOL};
use tungstenite::protocol::WebSocketConfig;
use tungstenite::stream::MaybeTlsStream;
use tungstenite::{client_tls_with_config, Error, Message, WebSocket};

struct Connection {
    socket: WebSocket<MaybeTlsStream<HandshakeStream>>,
    received: Vec<u8>,
}

const CONNECT_POLL_INTERVAL: Duration = Duration::from_millis(50);
const MAX_CONNECT_WORKERS: usize = 4;
static CONNECT_WORKERS: AtomicUsize = AtomicUsize::new(0);

type CancelCallback = Option<unsafe extern "C" fn(*mut c_void) -> bool>;

#[derive(Clone, Copy, Debug)]
struct Cancellation {
    callback: CancelCallback,
    user: *mut c_void,
}

impl Cancellation {
    fn canceled(self) -> bool {
        self.callback
            .is_some_and(|callback| unsafe { callback(self.user) })
    }

    fn remaining(self, deadline: Instant) -> io::Result<Duration> {
        if self.canceled() {
            return Err(io::Error::new(
                io::ErrorKind::ConnectionAborted,
                "连接已取消",
            ));
        }
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            return Err(io::Error::new(io::ErrorKind::TimedOut, "连接总超时"));
        }
        Ok(remaining.min(CONNECT_POLL_INTERVAL))
    }
}

// DNS 的平台调用无法中断；独立线程仅持有地址和结果，取消后不引用 C++ 对象。
// 同时限制线程数量，避免重连不断积累仍在解析的线程。
struct ConnectPermit;
impl Drop for ConnectPermit {
    fn drop(&mut self) {
        CONNECT_WORKERS.fetch_sub(1, Ordering::AcqRel);
    }
}

fn start_tcp_connect(
    host: String,
    port: u16,
    deadline: Instant,
) -> Result<Receiver<Result<TcpStream, String>>, String> {
    CONNECT_WORKERS
        .fetch_update(Ordering::AcqRel, Ordering::Acquire, |count| {
            (count < MAX_CONNECT_WORKERS).then_some(count + 1)
        })
        .map_err(|_| "等待前一次地址解析完成".to_string())?;
    let permit = ConnectPermit;
    let (sender, receiver) = mpsc::sync_channel(1);
    std::thread::Builder::new()
        .name("qm-ws-resolve".into())
        .spawn(move || {
            let _permit = permit;
            let result = (|| {
                let addresses = (host.as_str(), port)
                    .to_socket_addrs()
                    .map_err(|error| error.to_string())?;
                let mut last_error = "主机没有可用地址".to_string();
                for address in addresses {
                    let remaining = deadline.saturating_duration_since(Instant::now());
                    if remaining.is_zero() {
                        return Err("连接总超时".into());
                    }
                    match TcpStream::connect_timeout(&address, remaining) {
                        Ok(stream) => return Ok(stream),
                        Err(error) => last_error = error.to_string(),
                    }
                }
                Err(last_error)
            })();
            // 接收端已经取消时，未发布的 socket 随结果释放。
            let _ = sender.send(result);
        })
        .map_err(|error| error.to_string())?;
    Ok(receiver)
}

fn wait_tcp_connect(
    receiver: &Receiver<Result<TcpStream, String>>,
    deadline: Instant,
    cancellation: Cancellation,
) -> Result<TcpStream, String> {
    loop {
        let wait = cancellation
            .remaining(deadline)
            .map_err(|error| error.to_string())?;
        match receiver.recv_timeout(wait) {
            Ok(result) => return result,
            Err(RecvTimeoutError::Timeout) => continue,
            Err(RecvTimeoutError::Disconnected) => return Err("连接线程已停止".into()),
        }
    }
}

#[derive(Debug)]
struct HandshakeStream {
    tcp: TcpStream,
    deadline: Option<Instant>,
    cancellation: Cancellation,
}

impl Read for HandshakeStream {
    fn read(&mut self, output: &mut [u8]) -> io::Result<usize> {
        let Some(deadline) = self.deadline else {
            return self.tcp.read(output);
        };
        loop {
            self.tcp
                .set_read_timeout(Some(self.cancellation.remaining(deadline)?))?;
            match self.tcp.read(output) {
                Err(error)
                    if matches!(
                        error.kind(),
                        io::ErrorKind::TimedOut | io::ErrorKind::WouldBlock
                    ) =>
                {
                    continue
                }
                result => return result,
            }
        }
    }
}

impl Write for HandshakeStream {
    fn write(&mut self, input: &[u8]) -> io::Result<usize> {
        let Some(deadline) = self.deadline else {
            return self.tcp.write(input);
        };
        loop {
            self.tcp
                .set_write_timeout(Some(self.cancellation.remaining(deadline)?))?;
            match self.tcp.write(input) {
                Err(error)
                    if matches!(
                        error.kind(),
                        io::ErrorKind::TimedOut | io::ErrorKind::WouldBlock
                    ) =>
                {
                    continue
                }
                result => return result,
            }
        }
    }

    fn flush(&mut self) -> io::Result<()> {
        if let Some(deadline) = self.deadline {
            self.cancellation.remaining(deadline)?;
        }
        self.tcp.flush()
    }
}

unsafe fn text<'a>(value: *const c_char) -> Result<&'a str, String> {
    if value.is_null() {
        return Err("空字符串指针".into());
    }
    CStr::from_ptr(value).to_str().map_err(|e| e.to_string())
}

unsafe fn write_error(output: *mut c_char, size: usize, error: impl std::fmt::Display) {
    if !output.is_null() && size > 0 {
        let message = error.to_string();
        let length = message.len().min(size - 1);
        ptr::copy_nonoverlapping(message.as_ptr(), output.cast(), length);
        *output.add(length) = 0;
    }
}

fn connect(
    url: &str,
    protocol: &str,
    headers: &str,
    timeout_ms: u32,
    limit: usize,
    cancellation: Cancellation,
) -> Result<Connection, String> {
    let mut request = url.into_client_request().map_err(|e| e.to_string())?;
    if !protocol.is_empty() {
        request.headers_mut().insert(
            SEC_WEBSOCKET_PROTOCOL,
            HeaderValue::from_str(protocol).map_err(|e| e.to_string())?,
        );
    }
    for header in headers.lines() {
        let (name, value) = header.split_once(':').ok_or("请求头格式无效")?;
        request.headers_mut().append(
            HeaderName::from_bytes(name.trim().as_bytes()).map_err(|e| e.to_string())?,
            HeaderValue::from_str(value.trim()).map_err(|e| e.to_string())?,
        );
    }
    if !matches!(request.uri().scheme_str(), Some("ws" | "wss")) {
        return Err("只支持 ws/wss".into());
    }
    let host = request
        .uri()
        .host()
        .ok_or("缺少主机名")?
        .trim_start_matches('[')
        .trim_end_matches(']');
    let port = request
        .uri()
        .port_u16()
        .unwrap_or(if request.uri().scheme_str() == Some("wss") {
            443
        } else {
            80
        });
    let timeout = Duration::from_millis(u64::from(timeout_ms.max(1)));
    let deadline = Instant::now() + timeout;
    cancellation
        .remaining(deadline)
        .map_err(|error| error.to_string())?;
    let receiver = start_tcp_connect(host.to_owned(), port, deadline)?;
    let tcp = wait_tcp_connect(&receiver, deadline, cancellation)?;
    let stream = HandshakeStream {
        tcp,
        deadline: Some(deadline),
        cancellation,
    };
    let config = WebSocketConfig {
        max_message_size: Some(limit),
        max_frame_size: Some(limit),
        max_write_buffer_size: limit * 2 + 1024,
        ..Default::default()
    };
    // rustls 校验证书链、有效期和域名，所有平台使用同一套 Mozilla 信任根。
    let (mut socket, _) =
        client_tls_with_config(request, stream, Some(config), None).map_err(|e| e.to_string())?;
    cancellation
        .remaining(deadline)
        .map_err(|error| error.to_string())?;
    let stream = match socket.get_mut() {
        MaybeTlsStream::Plain(stream) => stream,
        MaybeTlsStream::Rustls(tls) => &mut tls.sock,
        _ => return Err("未知 TLS 后端".into()),
    };
    // 建连完成后释放取消回调，句柄不保留 C++ 生命周期指针。
    stream.deadline = None;
    stream.cancellation = Cancellation {
        callback: None,
        user: ptr::null_mut(),
    };
    stream
        .tcp
        .set_read_timeout(None)
        .map_err(|error| error.to_string())?;
    stream
        .tcp
        .set_write_timeout(None)
        .map_err(|error| error.to_string())?;
    stream
        .tcp
        .set_nonblocking(true)
        .map_err(|error| error.to_string())?;
    Ok(Connection {
        socket,
        received: Vec::new(),
    })
}

/// 建立连接；失败返回空句柄，错误写入调用方缓冲区。
#[no_mangle]
pub unsafe extern "C" fn qm_ws_connect(
    url: *const c_char,
    protocol: *const c_char,
    headers: *const c_char,
    timeout_ms: u32,
    limit: usize,
    error: *mut c_char,
    error_size: usize,
    canceled: CancelCallback,
    user: *mut c_void,
) -> *mut c_void {
    let result = (|| {
        connect(
            text(url)?,
            text(protocol)?,
            text(headers)?,
            timeout_ms,
            limit,
            Cancellation {
                callback: canceled,
                user,
            },
        )
    })();
    match result {
        Ok(connection) => Box::into_raw(Box::new(connection)).cast(),
        Err(reason) => {
            write_error(error, error_size, reason);
            ptr::null_mut()
        }
    }
}

/// 发送完整消息；0/1/2 分别表示文本、二进制、心跳。
#[no_mangle]
pub unsafe extern "C" fn qm_ws_send(
    handle: *mut c_void,
    kind: i32,
    data: *const u8,
    size: usize,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    let connection = &mut *handle.cast::<Connection>();
    let bytes = if size == 0 {
        &[]
    } else {
        std::slice::from_raw_parts(data, size)
    };
    let message = match kind {
        0 => match std::str::from_utf8(bytes) {
            Ok(value) => Message::Text(value.to_owned()),
            Err(reason) => {
                write_error(error, error_size, reason);
                return false;
            }
        },
        1 => Message::Binary(bytes.to_vec()),
        2 => Message::Ping(bytes.to_vec()),
        _ => return false,
    };
    match connection.socket.send(message) {
        Ok(()) => true,
        // tungstenite 已保留待发数据，后续 read/flush 继续发送，不能再次入队同一消息。
        Err(Error::Io(ref reason)) if reason.kind() == io::ErrorKind::WouldBlock => true,
        Err(reason) => {
            write_error(error, error_size, reason);
            false
        }
    }
}

/// 接收返回 0=暂无消息、1=文本、2=二进制、3=pong、-1=失败、-2=正常关闭。
/// 数据指针在下一次 read 或 close 前有效，调用方必须立即复制。
#[no_mangle]
pub unsafe extern "C" fn qm_ws_read(
    handle: *mut c_void,
    data: *mut *const u8,
    size: *mut usize,
    error: *mut c_char,
    error_size: usize,
) -> i32 {
    let connection = &mut *handle.cast::<Connection>();
    if let Err(reason) = connection.socket.flush() {
        if !matches!(&reason, Error::Io(io) if io.kind() == io::ErrorKind::WouldBlock) {
            write_error(error, error_size, reason);
            return -1;
        }
    }
    match connection.socket.read() {
        Ok(message) => {
            let kind = match &message {
                Message::Text(_) => 1,
                Message::Binary(_) => 2,
                Message::Pong(_) => 3,
                Message::Close(_) => return -2,
                _ => return 0,
            };
            connection.received = message.into_data();
            *data = connection.received.as_ptr();
            *size = connection.received.len();
            kind
        }
        Err(Error::Io(ref reason))
            if matches!(
                reason.kind(),
                io::ErrorKind::WouldBlock | io::ErrorKind::TimedOut
            ) =>
        {
            0
        }
        Err(Error::ConnectionClosed | Error::AlreadyClosed) => -2,
        Err(reason) => {
            write_error(error, error_size, reason);
            -1
        }
    }
}

/// 释放句柄前发送关闭帧；调用方须先完成待发送业务消息。
#[no_mangle]
pub unsafe extern "C" fn qm_ws_close(handle: *mut c_void) {
    if !handle.is_null() {
        let mut connection = Box::from_raw(handle.cast::<Connection>());
        let _ = connection.socket.close(None);
        let deadline = Instant::now() + Duration::from_secs(1);
        while Instant::now() < deadline {
            match connection.socket.flush() {
                Err(Error::Io(ref reason)) if reason.kind() == io::ErrorKind::WouldBlock => {
                    std::thread::sleep(Duration::from_millis(5));
                }
                _ => break,
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn invalid_header_fails_before_network() {
        assert!(connect(
            "wss://qmclient.icu/ws",
            "qmclient-json",
            "invalid",
            1,
            1024,
            Cancellation {
                callback: None,
                user: ptr::null_mut()
            }
        )
        .is_err());
    }

    #[test]
    fn invalid_scheme_is_rejected() {
        assert!(connect(
            "https://qmclient.icu/ws",
            "",
            "",
            1,
            1024,
            Cancellation {
                callback: None,
                user: ptr::null_mut()
            }
        )
        .is_err());
    }

    fn no_cancellation() -> Cancellation {
        Cancellation {
            callback: None,
            user: ptr::null_mut(),
        }
    }

    unsafe extern "C" fn is_canceled(user: *mut c_void) -> bool {
        (*(user.cast::<std::sync::atomic::AtomicBool>())).load(Ordering::Acquire)
    }

    // 动态端口的真实 TCP peer；退出和 accept/read/write 都有明确期限。
    #[derive(Clone, Copy)]
    enum PeerBehavior {
        Cancel,
        Drip,
        Accept,
    }

    struct LocalPeer {
        port: u16,
        cancel: std::sync::Arc<std::sync::atomic::AtomicBool>,
        stop: std::sync::Arc<std::sync::atomic::AtomicBool>,
        worker: Option<std::thread::JoinHandle<()>>,
    }

    impl LocalPeer {
        fn start(behavior: PeerBehavior) -> Self {
            use std::net::TcpListener;
            use std::sync::{atomic::AtomicBool, Arc};
            let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
            let port = listener.local_addr().unwrap().port();
            listener.set_nonblocking(true).unwrap();
            let cancel = Arc::new(AtomicBool::new(false));
            let stop = Arc::new(AtomicBool::new(false));
            let peer_cancel = Arc::clone(&cancel);
            let peer_stop = Arc::clone(&stop);
            let worker = std::thread::spawn(move || {
                let deadline = Instant::now() + Duration::from_secs(3);
                while !peer_stop.load(Ordering::Acquire) && Instant::now() < deadline {
                    let (mut stream, _) = match listener.accept() {
                        Ok(connection) => connection,
                        Err(error) if error.kind() == io::ErrorKind::WouldBlock => {
                            std::thread::sleep(Duration::from_millis(2));
                            continue;
                        }
                        Err(_) => return,
                    };
                    stream.set_nonblocking(false).unwrap();
                    stream
                        .set_read_timeout(Some(CONNECT_POLL_INTERVAL))
                        .unwrap();
                    stream
                        .set_write_timeout(Some(CONNECT_POLL_INTERVAL))
                        .unwrap();
                    if matches!(behavior, PeerBehavior::Accept) {
                        stream
                            .set_read_timeout(Some(Duration::from_secs(1)))
                            .unwrap();
                        stream
                            .set_write_timeout(Some(Duration::from_secs(1)))
                            .unwrap();
                        let mut socket = tungstenite::accept(stream).unwrap();
                        socket.send(Message::Text("hello".into())).unwrap();
                        return;
                    }
                    let drip_response = matches!(behavior, PeerBehavior::Drip);
                    let mut input = [0; 4096];
                    loop {
                        if peer_stop.load(Ordering::Acquire) || Instant::now() >= deadline {
                            return;
                        }
                        match stream.read(&mut input) {
                            Ok(0) => return,
                            Ok(_) => break,
                            Err(error)
                                if matches!(
                                    error.kind(),
                                    io::ErrorKind::TimedOut | io::ErrorKind::WouldBlock
                                ) =>
                            {
                                continue
                            }
                            Err(_) => return,
                        }
                    }
                    if !drip_response {
                        peer_cancel.store(true, Ordering::Release);
                    } else if stream
                        .write_all(b"HTTP/1.1 101 Switching Protocols\r\nX-Slow: ")
                        .is_err()
                    {
                        return;
                    }
                    while !peer_stop.load(Ordering::Acquire) && Instant::now() < deadline {
                        if drip_response && stream.write_all(b"x").is_err() {
                            return;
                        }
                        std::thread::sleep(Duration::from_millis(10));
                    }
                    return;
                }
            });
            Self {
                port,
                cancel,
                stop,
                worker: Some(worker),
            }
        }

        fn cancellation(&self) -> Cancellation {
            Cancellation {
                callback: Some(is_canceled),
                user: std::sync::Arc::as_ptr(&self.cancel).cast_mut().cast(),
            }
        }
    }

    impl Drop for LocalPeer {
        fn drop(&mut self) {
            self.stop.store(true, Ordering::Release);
            self.worker.take().unwrap().join().unwrap();
        }
    }

    #[test]
    fn cancel_while_waiting_for_resolver_does_not_wait_for_result() {
        use std::sync::atomic::AtomicBool;
        let canceled = AtomicBool::new(true);
        let (_sender, receiver) = mpsc::sync_channel(1);
        let cancellation = Cancellation {
            callback: Some(is_canceled),
            user: (&canceled as *const AtomicBool).cast_mut().cast(),
        };
        let start = Instant::now();
        assert!(wait_tcp_connect(&receiver, start + Duration::from_secs(5), cancellation).is_err());
        assert!(start.elapsed() < Duration::from_secs(1));
    }

    #[test]
    fn cancel_interrupts_plain_websocket_handshake() {
        let peer = LocalPeer::start(PeerBehavior::Cancel);
        let start = Instant::now();
        let result = connect(
            &format!("ws://127.0.0.1:{}/", peer.port),
            "",
            "",
            5000,
            1024 * 1024,
            peer.cancellation(),
        );
        assert!(peer.cancel.load(Ordering::Acquire));
        assert!(result
            .err()
            .is_some_and(|reason| reason.contains("连接已取消")));
        assert!(start.elapsed() < Duration::from_secs(2));
    }

    #[test]
    fn cancel_interrupts_tls_handshake_before_server_responds() {
        let peer = LocalPeer::start(PeerBehavior::Cancel);
        let start = Instant::now();
        let result = connect(
            &format!("wss://127.0.0.1:{}/", peer.port),
            "",
            "",
            5000,
            1024 * 1024,
            peer.cancellation(),
        );
        assert!(peer.cancel.load(Ordering::Acquire));
        assert!(result
            .err()
            .is_some_and(|reason| reason.contains("连接已取消")));
        assert!(start.elapsed() < Duration::from_secs(2));
    }

    #[test]
    fn slow_response_cannot_extend_overall_handshake_deadline() {
        let peer = LocalPeer::start(PeerBehavior::Drip);
        let start = Instant::now();
        let result = connect(
            &format!("ws://127.0.0.1:{}/", peer.port),
            "",
            "",
            200,
            1024 * 1024,
            no_cancellation(),
        );
        assert!(result
            .err()
            .is_some_and(|reason| reason.contains("连接总超时")));
        assert!(start.elapsed() < Duration::from_secs(2));
        assert!(!peer.cancel.load(Ordering::Acquire));
    }

    #[test]
    fn successful_connection_releases_handshake_cancellation_callback() {
        let peer = LocalPeer::start(PeerBehavior::Accept);
        let mut connection = connect(
            &format!("ws://127.0.0.1:{}/", peer.port),
            "",
            "",
            2000,
            1024 * 1024,
            peer.cancellation(),
        )
        .ok()
        .expect("local websocket handshake succeeds");
        peer.cancel.store(true, Ordering::Release);
        let deadline = Instant::now() + Duration::from_secs(1);
        loop {
            match connection.socket.read() {
                Ok(Message::Text(text)) => {
                    assert_eq!(text, "hello");
                    break;
                }
                Err(Error::Io(error))
                    if error.kind() == io::ErrorKind::WouldBlock && Instant::now() < deadline =>
                {
                    std::thread::sleep(Duration::from_millis(2));
                }
                result => panic!("unexpected receive result: {result:?}"),
            }
        }
    }
}
