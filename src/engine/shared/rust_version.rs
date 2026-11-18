use super::CFGFLAG_CLIENT;
use super::CFGFLAG_SERVER;
use ddnet_base::s;
use ddnet_base::UserPtr;
use ddnet_engine::gs_ConsoleDefaultColor;
use ddnet_engine::IConsole;
use ddnet_engine::IConsole_FCommandCallback;
use ddnet_engine::IConsole_IResult;
use ddnet_engine::IConsole_OUTPUT_LEVEL_STANDARD;
use ddnet_engine::TryRegisterRustConsoleCommand;
use std::pin::Pin;

#[cxx::bridge]
mod ffi {
    extern "C++" {
        include!("base/rust.h");
        include!("engine/console.h");

        type IConsole = ddnet_engine::IConsole;
    }
    extern "Rust" {
        fn RustVersionPrint(console: &IConsole);
        fn RustVersionRegister(console: Pin<&mut IConsole>);
    }
}

/// Print the Rust version used for compiling this crate.
///
/// Uses [`IConsole::Print`] for printing.
#[allow(non_snake_case)]
pub fn RustVersionPrint(console: &IConsole) {
    console.Print(
        IConsole_OUTPUT_LEVEL_STANDARD,
        s!("rust_version"),
        s!(include_str!(concat!(env!("OUT_DIR"), "/rustc-version"))),
        gs_ConsoleDefaultColor,
    );
}

#[allow(non_snake_case)]
extern "C" fn PrintRustVersionCallback(_: &IConsole_IResult, user: UserPtr) {
    RustVersionPrint(unsafe { user.cast() })
}

/// Register the `rust_version` command to the given console instance.
///
/// This command calls the [`RustVersionPrint`] function to print the Rust
/// version used for compiling this crate.
#[allow(non_snake_case)]
pub fn RustVersionRegister(console: Pin<&mut IConsole>) {
    let user = console.as_ref().get_ref().into();
    // SAFETY: 用户指针借用的是持有该命令的控制台。
    // 节点分配失败时略过可选的版本命令，继续初始化。
    let _ = unsafe {
        TryRegisterRustConsoleCommand(
            console,
            s!("rust_version"),
            s!(""),
            CFGFLAG_CLIENT | CFGFLAG_SERVER,
            IConsole_FCommandCallback(PrintRustVersionCallback),
            user,
            s!("Prints the Rust version used to compile DDNet"),
        )
    };
}
