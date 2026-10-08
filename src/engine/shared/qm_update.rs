use std::ffi::c_char;

/// Verifies an in-memory signed update manifest.
#[no_mangle]
pub unsafe extern "C" fn qm_update_verify_manifest(
    manifest: *const u8,
    manifest_size: usize,
    signature: *const u8,
    signature_size: usize,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    ::qm_update::ffi_verify_manifest(
        manifest,
        manifest_size,
        signature,
        signature_size,
        error,
        error_size,
    )
}

/// Verifies a manifest and returns its signed package size and SHA-256 digest.
#[no_mangle]
pub unsafe extern "C" fn qm_update_verify_manifest_package(
    manifest: *const u8,
    manifest_size: usize,
    signature: *const u8,
    signature_size: usize,
    package_size: *mut u64,
    package_digest: *mut u8,
    package_digest_size: usize,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    ::qm_update::ffi_verify_manifest_package(
        manifest,
        manifest_size,
        signature,
        signature_size,
        package_size,
        package_digest,
        package_digest_size,
        error,
        error_size,
    )
}

/// Verifies the detached Ed25519 signature for a package SHA-256 digest.
#[no_mangle]
pub unsafe extern "C" fn qm_update_verify_package_digest(
    digest: *const u8,
    digest_size: usize,
    signature: *const u8,
    signature_size: usize,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    ::qm_update::ffi_verify_package_digest(
        digest,
        digest_size,
        signature,
        signature_size,
        error,
        error_size,
    )
}

/// Extracts the updater executable after verifying its signed manifest entry.
#[no_mangle]
pub unsafe extern "C" fn qm_update_extract_bootstrap_updater(
    package_path: *const c_char,
    manifest: *const u8,
    manifest_size: usize,
    signature: *const u8,
    signature_size: usize,
    destination: *const c_char,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    ::qm_update::ffi_extract_bootstrap_updater(
        package_path,
        manifest,
        manifest_size,
        signature,
        signature_size,
        destination,
        error,
        error_size,
    )
}

/// 校验独立 Setup 清单，保持旧 ZIP 清单接口兼容。
#[no_mangle]
pub unsafe extern "C" fn qm_update_verify_setup_manifest(
    manifest: *const u8,
    manifest_size: usize,
    signature: *const u8,
    signature_size: usize,
    package_size: *mut u64,
    package_digest: *mut u8,
    package_digest_size: usize,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    ::qm_update::ffi_verify_setup_manifest(
        manifest,
        manifest_size,
        signature,
        signature_size,
        package_size,
        package_digest,
        package_digest_size,
        error,
        error_size,
    )
}

// Setup 文件验签导出由 qm-update 自身提供，独立安装器也能链接同一入口。
