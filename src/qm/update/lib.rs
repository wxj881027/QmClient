//! Signed full-package update verification and transactional installation.

use ed25519_dalek::{Signature, Verifier, VerifyingKey};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::collections::{HashMap, HashSet};
use std::ffi::{c_char, CStr};
use std::fs::{self, File};
use std::io::{self, Read, Write};
use std::path::{Component, Path, PathBuf};
use std::slice;
use std::time::{SystemTime, UNIX_EPOCH};
use zip::ZipArchive;

const PUBLIC_KEY: [u8; 32] = [
    88, 90, 115, 247, 47, 84, 75, 33, 140, 114, 135, 159, 223, 101, 133, 81, 150, 125, 144, 190,
    246, 220, 67, 117, 92, 49, 2, 211, 109, 81, 45, 156,
];
const PACKAGE_SIGNATURE_CONTEXT: &[u8] = b"QmClient update package SHA-256\0";
const MAX_MANIFEST_SIZE: usize = 32 * 1024 * 1024;
const MAX_PACKAGE_SIZE: u64 = 5 * 1024 * 1024 * 1024;
const MAX_FILE_SIZE: u64 = 2 * 1024 * 1024 * 1024;
const MAX_TOTAL_SIZE: u64 = 10 * 1024 * 1024 * 1024;
const MAX_FILE_COUNT: usize = 100_000;
const PERMISSION_DENIED_MARKER: &str = "QM_UPDATE_PERMISSION_DENIED:";

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Manifest {
    schema: u32,
    version: String,
    package: Package,
    files: Vec<ManifestFile>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Package {
    name: String,
    size: u64,
    sha256: String,
}

#[derive(Clone, Deserialize)]
#[serde(deny_unknown_fields)]
struct ManifestFile {
    path: String,
    size: u64,
    sha256: String,
}

fn validate_version(version: &str) -> Result<(), String> {
    version_parts(version).map(|_| ())
}

fn version_parts(version: &str) -> Result<([i32; 4], bool, i32), String> {
    let (base, preview) = match version.split_once("-preview.") {
        Some((base, number)) => {
            if number.is_empty()
                || number.starts_with('0')
                || !number.bytes().all(|byte| byte.is_ascii_digit())
            {
                return Err("the update preview number is invalid".into());
            }
            let number = number
                .parse::<i32>()
                .map_err(|_| "the update preview number is too large")?;
            (base, Some(number))
        }
        None => (version, None),
    };
    let parts: Vec<_> = base.split('.').collect();
    if version.len() >= 32
        || !(1..=4).contains(&parts.len())
        || (preview.is_some() && parts.len() != 2)
        || parts
            .iter()
            .any(|part| part.is_empty() || !part.bytes().all(|byte| byte.is_ascii_digit()))
    {
        return Err("the update version is invalid".into());
    }
    let mut result = [0; 4];
    for (index, part) in parts.into_iter().enumerate() {
        result[index] = part
            .parse()
            .map_err(|_| "the update version component is too large")?;
    }
    Ok((result, preview.is_none(), preview.unwrap_or(0)))
}

fn validate_not_downgrade(update_version: &str, current_version: &str) -> Result<(), String> {
    if version_parts(update_version)? < version_parts(current_version)? {
        return Err(
            "the signed update version is older than the installed QmClient version".into(),
        );
    }
    Ok(())
}

fn validate_relative_path(value: &str) -> Result<PathBuf, String> {
    if value.is_empty()
        || value.len() > 1024
        || value.contains('\\')
        || value.contains(':')
        || value.contains('\0')
    {
        return Err(format!("unsafe update path: {value:?}"));
    }
    // 发布更新不能覆盖便携版的用户存档。
    if value
        .split('/')
        .next()
        .is_some_and(|part| part.eq_ignore_ascii_case("profile"))
    {
        return Err("update package must not contain user profile files".into());
    }
    let path = Path::new(value);
    if path.is_absolute()
        || path
            .components()
            .any(|component| !matches!(component, Component::Normal(_)))
    {
        return Err(format!("unsafe update path: {value:?}"));
    }
    for segment in value.split('/') {
        if segment.is_empty() || segment.ends_with(['.', ' ']) {
            return Err(format!("unsafe update path: {value:?}"));
        }
        let stem = segment
            .split('.')
            .next()
            .unwrap_or(segment)
            .to_ascii_uppercase();
        if matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL")
            || (stem.len() == 4
                && (stem.starts_with("COM") || stem.starts_with("LPT"))
                && matches!(stem.as_bytes()[3], b'1'..=b'9'))
        {
            return Err(format!("reserved Windows update path: {value:?}"));
        }
    }
    Ok(path.to_path_buf())
}

fn parse_hash(value: &str) -> Result<[u8; 32], String> {
    if value.len() != 64 || !value.bytes().all(|byte| byte.is_ascii_hexdigit()) {
        return Err("invalid SHA-256 value in update manifest".into());
    }
    let mut result = [0; 32];
    for (index, output) in result.iter_mut().enumerate() {
        *output = u8::from_str_radix(&value[index * 2..index * 2 + 2], 16)
            .map_err(|_| "invalid SHA-256 value in update manifest")?;
    }
    Ok(result)
}

fn parse_manifest(bytes: &[u8]) -> Result<Manifest, String> {
    if bytes.is_empty() || bytes.len() > MAX_MANIFEST_SIZE {
        return Err("update manifest size is outside the supported range".into());
    }
    let manifest: Manifest = serde_json::from_slice(bytes)
        .map_err(|error| format!("invalid update manifest: {error}"))?;
    if manifest.schema != 1 {
        return Err("unsupported update manifest schema".into());
    }
    validate_version(&manifest.version)?;
    if !matches!(
        manifest.package.name.as_str(),
        "QmClient-windows.zip"
            | "QmClient-windows-portable.zip"
            | "QmClient-windows.7z"
            | "QmClient-windows-portable.7z"
    ) || manifest.package.size == 0
        || manifest.package.size > MAX_PACKAGE_SIZE
    {
        return Err("invalid update package metadata".into());
    }
    parse_hash(&manifest.package.sha256)?;
    if manifest.files.is_empty() || manifest.files.len() > MAX_FILE_COUNT {
        return Err("update manifest file count is outside the supported range".into());
    }
    let mut seen = HashSet::new();
    let mut total_size = 0u64;
    for file in &manifest.files {
        validate_relative_path(&file.path)?;
        let folded = file.path.to_lowercase();
        if !seen.insert(folded) {
            return Err(format!("duplicate update path: {}", file.path));
        }
        if file.size > MAX_FILE_SIZE {
            return Err(format!("update file is too large: {}", file.path));
        }
        total_size = total_size
            .checked_add(file.size)
            .ok_or_else(|| "update extraction size overflow".to_string())?;
        if total_size > MAX_TOTAL_SIZE {
            return Err("update extraction size exceeds the supported limit".into());
        }
        parse_hash(&file.sha256)?;
    }
    for required in ["DDNet.exe", "DDNet-Server.exe", "QmClient-Updater.exe"] {
        if !manifest.files.iter().any(|file| file.path == required) {
            return Err(format!("update package is missing {required}"));
        }
    }
    Ok(manifest)
}

fn verify_signature_with_key(
    message: &[u8],
    signature_bytes: &[u8],
    public_key: &[u8; 32],
) -> Result<(), String> {
    let verifying_key =
        VerifyingKey::from_bytes(public_key).map_err(|_| "invalid embedded update public key")?;
    let signature = Signature::from_slice(signature_bytes)
        .map_err(|_| "invalid Ed25519 update signature length")?;
    verifying_key
        .verify(message, &signature)
        .map_err(|_| "Ed25519 update signature verification failed".into())
}

fn verify_manifest(bytes: &[u8], signature: &[u8]) -> Result<Manifest, String> {
    verify_signature_with_key(bytes, signature, &PUBLIC_KEY)?;
    parse_manifest(bytes)
}

fn package_signature_message(digest: &[u8; 32]) -> Vec<u8> {
    let mut message = Vec::with_capacity(PACKAGE_SIGNATURE_CONTEXT.len() + digest.len());
    message.extend_from_slice(PACKAGE_SIGNATURE_CONTEXT);
    message.extend_from_slice(digest);
    message
}

fn format_io_error(context: &str, error: &io::Error) -> String {
    if error.kind() == io::ErrorKind::PermissionDenied {
        format!("{PERMISSION_DENIED_MARKER} {context}: {error}")
    } else {
        format!("{context}: {error}")
    }
}

#[cfg(windows)]
fn is_reparse_point(metadata: &fs::Metadata) -> bool {
    use std::os::windows::fs::MetadataExt;

    const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x400;
    metadata.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT != 0
}

#[cfg(not(windows))]
fn is_reparse_point(metadata: &fs::Metadata) -> bool {
    metadata.file_type().is_symlink()
}

fn path_metadata(path: &Path, context: &str) -> Result<Option<fs::Metadata>, String> {
    match fs::symlink_metadata(path) {
        Ok(metadata) => Ok(Some(metadata)),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(format_io_error(context, &error)),
    }
}

fn validate_regular_file(path: &Path, context: &str) -> Result<(), String> {
    let metadata =
        path_metadata(path, context)?.ok_or_else(|| format!("{context}: file does not exist"))?;
    if !metadata.is_file() || is_reparse_point(&metadata) {
        return Err(format!("{context}: file is not a regular file"));
    }
    Ok(())
}

fn validate_install_root(install: &Path) -> Result<(), String> {
    let metadata = path_metadata(install, "failed inspecting the QmClient install directory")?
        .ok_or_else(|| "the QmClient install directory does not exist".to_string())?;
    if !metadata.is_dir() || is_reparse_point(&metadata) {
        return Err("the QmClient install directory is not a regular directory".into());
    }
    validate_regular_file(
        &install.join("DDNet.exe"),
        "the QmClient install directory is missing DDNet.exe",
    )?;
    validate_regular_file(
        &install.join("data/qmclient/gui_logo.png"),
        "the QmClient install directory is missing data/qmclient/gui_logo.png",
    )?;
    Ok(())
}

fn validate_install_target(install: &Path, relative: &Path) -> Result<(), String> {
    let components: Vec<_> = relative.components().collect();
    let mut current = install.to_path_buf();
    for (index, component) in components.iter().enumerate() {
        current.push(component.as_os_str());
        let metadata = match path_metadata(&current, "failed inspecting an update target")? {
            Some(metadata) => metadata,
            None => continue,
        };
        if is_reparse_point(&metadata) {
            return Err(format!(
                "update target contains a symbolic link or reparse point: {}",
                current.display()
            ));
        }
        if index + 1 == components.len() {
            if !metadata.is_file() {
                return Err(format!(
                    "update target is not a regular file: {}",
                    current.display()
                ));
            }
        } else if !metadata.is_dir() {
            return Err(format!(
                "update target parent is not a directory: {}",
                current.display()
            ));
        }
    }
    Ok(())
}

fn hash_reader(mut reader: impl Read) -> Result<([u8; 32], u64), String> {
    let mut hasher = Sha256::new();
    let mut buffer = vec![0; 1024 * 1024];
    let mut size = 0u64;
    loop {
        let read = reader
            .read(&mut buffer)
            .map_err(|error| format_io_error("failed reading update data", &error))?;
        if read == 0 {
            break;
        }
        size = size
            .checked_add(read as u64)
            .ok_or_else(|| "update data size overflow".to_string())?;
        hasher.update(&buffer[..read]);
    }
    Ok((hasher.finalize().into(), size))
}

fn read_limited(path: &Path, limit: usize) -> Result<Vec<u8>, String> {
    validate_regular_file(path, &format!("failed reading {}", path.display()))?;
    let metadata = fs::metadata(path)
        .map_err(|error| format_io_error(&format!("failed reading {}", path.display()), &error))?;
    if metadata.len() > limit as u64 {
        return Err(format!("{} is too large", path.display()));
    }
    fs::read(path)
        .map_err(|error| format_io_error(&format!("failed reading {}", path.display()), &error))
}

fn validate_package(
    package_path: &Path,
    package_signature: &[u8],
    manifest: &Manifest,
) -> Result<(), String> {
    validate_package_with_key(package_path, package_signature, manifest, &PUBLIC_KEY)
}

fn validate_package_with_key(
    package_path: &Path,
    package_signature: &[u8],
    manifest: &Manifest,
    key: &[u8; 32],
) -> Result<(), String> {
    validate_regular_file(package_path, "failed opening update package")?;
    let package = File::open(package_path)
        .map_err(|error| format_io_error("failed opening update package", &error))?;
    let (digest, size) = hash_reader(package)?;
    if size != manifest.package.size || digest != parse_hash(&manifest.package.sha256)? {
        return Err("update package size or SHA-256 does not match the signed manifest".into());
    }
    verify_signature_with_key(&package_signature_message(&digest), package_signature, key)
}

fn copy_exact(
    mut source: impl Read,
    mut target: impl Write,
    expected_size: u64,
) -> Result<[u8; 32], String> {
    let mut hasher = Sha256::new();
    let mut buffer = vec![0; 1024 * 1024];
    let mut size = 0u64;
    loop {
        let read = source
            .read(&mut buffer)
            .map_err(|error| format_io_error("failed extracting update file", &error))?;
        if read == 0 {
            break;
        }
        size = size
            .checked_add(read as u64)
            .ok_or_else(|| "extracted update file size overflow".to_string())?;
        if size > expected_size {
            return Err("extracted update file exceeds its signed size".into());
        }
        target
            .write_all(&buffer[..read])
            .map_err(|error| format_io_error("failed writing staged update file", &error))?;
        hasher.update(&buffer[..read]);
    }
    if size != expected_size {
        return Err("extracted update file size does not match the signed manifest".into());
    }
    Ok(hasher.finalize().into())
}

fn unique_child(root: &Path, prefix: &str) -> Result<PathBuf, String> {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| "system clock is before the Unix epoch")?
        .as_nanos();
    for attempt in 0..32 {
        let path = root.join(format!(
            ".{prefix}-{}-{nonce}-{attempt}",
            std::process::id()
        ));
        match fs::create_dir(&path) {
            Ok(()) => return Ok(path),
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => continue,
            Err(error) => {
                return Err(format_io_error(
                    &format!("failed creating {}", path.display()),
                    &error,
                ));
            }
        }
    }
    Err("failed creating a unique update working directory".into())
}

// 7z 的启动器提取和完整解包共用同一条签名路径与属性规则。
fn validate_sevenzip_entry<'a>(
    entry: &sevenz_rust::SevenZArchiveEntry,
    expected: &HashMap<String, &'a ManifestFile>,
    seen: &mut HashSet<String>,
) -> Result<Option<(&'a ManifestFile, PathBuf)>, String> {
    let relative = validate_relative_path(&entry.name)?;
    let unix_kind = (entry.windows_attributes >> 16) & 0o170000;
    if entry.windows_attributes & 0x400 != 0 || unix_kind == 0o120000 || entry.is_anti_item {
        return Err(format!("invalid 7z entry attributes: {}", entry.name));
    }
    if entry.is_directory {
        return Ok(None);
    }
    let key = entry.name.to_lowercase();
    let signed = *expected
        .get(&key)
        .ok_or_else(|| format!("7z contains an unsigned file: {}", entry.name))?;
    if signed.path != entry.name || !seen.insert(key) || entry.size != signed.size {
        return Err(format!(
            "duplicate, case-mismatched or wrong-sized 7z entry: {}",
            entry.name
        ));
    }
    Ok(Some((signed, relative)))
}

// 不使用库的默认落盘回调，所有落盘内容必须在签名清单中。
fn extract_sevenzip(
    package_path: &Path,
    staging: &Path,
    manifest: &Manifest,
) -> Result<(), String> {
    validate_regular_file(package_path, "failed opening update package")?;
    let package =
        File::open(package_path).map_err(|e| format_io_error("failed opening 7z package", &e))?;
    let length = package.metadata().map_err(|e| e.to_string())?.len();
    let mut archive =
        sevenz_rust::SevenZReader::new(package, length, sevenz_rust::Password::empty())
            .map_err(|e| format!("invalid 7z update package: {e}"))?;
    let expected: HashMap<_, _> = manifest
        .files
        .iter()
        .map(|f| (f.path.to_lowercase(), f))
        .collect();
    let mut extracted = HashSet::new();
    archive
        .for_each_entries(|entry, reader| {
            let action = (|| -> Result<(), String> {
                let Some((signed, relative)) =
                    validate_sevenzip_entry(entry, &expected, &mut extracted)?
                else {
                    return Ok(());
                };
                let path = staging.join(relative);
                if let Some(parent) = path.parent() {
                    fs::create_dir_all(parent)
                        .map_err(|e| format_io_error("failed creating staging directory", &e))?;
                }
                let mut output = File::create(path)
                    .map_err(|e| format_io_error("failed creating 7z output", &e))?;
                let digest = copy_exact(reader, &mut output, signed.size)?;
                output.flush().map_err(|e| e.to_string())?;
                if digest != parse_hash(&signed.sha256)? {
                    return Err(format!("staged file hash mismatch: {}", signed.path));
                }
                Ok(())
            })();
            action.map(|_| true).map_err(sevenz_rust::Error::other)
        })
        .map_err(|e| e.to_string())?;
    if extracted.len() != expected.len() {
        return Err("7z does not contain every file in the signed manifest".into());
    }
    Ok(())
}

fn extract_package(package_path: &Path, staging: &Path, manifest: &Manifest) -> Result<(), String> {
    if manifest.package.name.ends_with(".7z") {
        return extract_sevenzip(package_path, staging, manifest);
    }
    validate_regular_file(package_path, "failed opening update package")?;
    let package = File::open(package_path)
        .map_err(|error| format_io_error("failed opening update package", &error))?;
    let mut archive =
        ZipArchive::new(package).map_err(|error| format!("invalid ZIP update package: {error}"))?;
    let expected: HashMap<_, _> = manifest
        .files
        .iter()
        .map(|file| (file.path.to_lowercase(), file))
        .collect();
    let mut extracted = HashSet::new();
    for index in 0..archive.len() {
        let mut entry = archive
            .by_index(index)
            .map_err(|error| format!("failed reading ZIP entry: {error}"))?;
        if entry.is_dir() {
            continue;
        }
        if entry
            .unix_mode()
            .map_or(false, |mode| mode & 0o170000 == 0o120000)
        {
            return Err(format!("symbolic links are not allowed: {}", entry.name()));
        }
        let relative = validate_relative_path(entry.name())?;
        let key = entry.name().to_lowercase();
        let signed = expected
            .get(&key)
            .ok_or_else(|| format!("ZIP contains an unsigned file: {}", entry.name()))?;
        if signed.path != entry.name() || !extracted.insert(key) {
            return Err(format!(
                "duplicate or case-mismatched ZIP path: {}",
                entry.name()
            ));
        }
        if entry.size() != signed.size {
            return Err(format!(
                "ZIP size does not match manifest: {}",
                entry.name()
            ));
        }
        let output_path = staging.join(relative);
        if let Some(parent) = output_path.parent() {
            fs::create_dir_all(parent)
                .map_err(|error| format_io_error("failed creating staging directory", &error))?;
        }
        let mut output = File::create(&output_path)
            .map_err(|error| format_io_error("failed creating staged update file", &error))?;
        let extracted_digest = copy_exact(&mut entry, &mut output, signed.size)?;
        output
            .flush()
            .map_err(|error| format_io_error("failed flushing staged update file", &error))?;
        if extracted_digest != parse_hash(&signed.sha256)? {
            return Err(format!("staged file hash mismatch: {}", signed.path));
        }
    }
    if extracted.len() != expected.len() {
        return Err("ZIP does not contain every file in the signed manifest".into());
    }
    Ok(())
}

// 启动阶段只解码 updater 所在的压缩块，完整逐文件校验由独立安装器执行。
fn extract_sevenzip_bootstrap(
    package_path: &Path,
    destination: &Path,
    manifest: &Manifest,
) -> Result<(), String> {
    validate_regular_file(package_path, "failed opening update package")?;
    let archive = sevenz_rust::Archive::open(package_path).map_err(|e| e.to_string())?;
    let expected: HashMap<_, _> = manifest
        .files
        .iter()
        .map(|f| (f.path.to_lowercase(), f))
        .collect();
    let mut seen = HashSet::new();
    let mut updater_index = None;
    for (index, entry) in archive.files.iter().enumerate() {
        if validate_sevenzip_entry(entry, &expected, &mut seen)?.is_some()
            && entry.name == "QmClient-Updater.exe"
        {
            updater_index = Some(index);
        }
    }
    if seen.len() != expected.len() {
        return Err("7z does not contain every file in the signed manifest".into());
    }
    let index = updater_index.ok_or("7z is missing QmClient-Updater.exe")?;
    let folder =
        archive.stream_map.file_folder_index[index].ok_or("updater has no executable content")?;
    let mut source = File::open(package_path).map_err(|e| e.to_string())?;
    let decoder = sevenz_rust::BlockDecoder::new(folder, &archive, &[], &mut source);
    let mut written = false;
    let result = decoder.for_each_entries(&mut |entry, reader| {
        if entry.name != "QmClient-Updater.exe" {
            io::copy(reader, &mut io::sink())
                .map_err(|e| sevenz_rust::Error::other(e.to_string()))?;
            return Ok(true);
        }
        let signed = expected.get(&entry.name.to_lowercase()).unwrap();
        let result = (|| -> Result<(), String> {
            let mut output = File::create(destination).map_err(|e| e.to_string())?;
            let digest = copy_exact(reader, &mut output, signed.size)?;
            output.flush().map_err(|e| e.to_string())?;
            if digest != parse_hash(&signed.sha256)? {
                return Err("bootstrap updater hash mismatch".into());
            }
            Ok(())
        })();
        result.map_err(sevenz_rust::Error::other)?;
        written = true;
        Ok(false)
    });
    if let Err(error) = result {
        let _ = fs::remove_file(destination);
        return Err(error.to_string());
    }
    if !written {
        return Err("7z is missing updater content".into());
    }
    Ok(())
}

fn extract_bootstrap_updater(
    package_path: &Path,
    destination: &Path,
    manifest: &Manifest,
) -> Result<(), String> {
    const UPDATER_NAME: &str = "QmClient-Updater.exe";
    let signed = manifest
        .files
        .iter()
        .find(|file| file.path == UPDATER_NAME)
        .ok_or_else(|| format!("update package is missing {UPDATER_NAME}"))?;
    if manifest.package.name.ends_with(".7z") {
        return extract_sevenzip_bootstrap(package_path, destination, manifest);
    }
    validate_regular_file(package_path, "failed opening update package")?;
    let package = File::open(package_path)
        .map_err(|error| format_io_error("failed opening update package", &error))?;
    let mut archive =
        ZipArchive::new(package).map_err(|error| format!("invalid ZIP update package: {error}"))?;
    let mut updater_index = None;
    for index in 0..archive.len() {
        let entry = archive
            .by_index(index)
            .map_err(|error| format!("failed reading ZIP entry: {error}"))?;
        if entry.name().eq_ignore_ascii_case(UPDATER_NAME) {
            if entry.name() != UPDATER_NAME || updater_index.replace(index).is_some() {
                return Err("duplicate or case-mismatched updater in ZIP".into());
            }
        }
    }
    let index = updater_index.ok_or_else(|| format!("ZIP is missing {UPDATER_NAME}"))?;
    let mut entry = archive
        .by_index(index)
        .map_err(|error| format!("failed reading updater from ZIP: {error}"))?;
    if entry.is_dir()
        || entry
            .unix_mode()
            .map_or(false, |mode| mode & 0o170000 == 0o120000)
        || entry.size() != signed.size
    {
        return Err("invalid updater entry in ZIP".into());
    }
    let result = (|| {
        let mut output = File::create(destination)
            .map_err(|error| format_io_error("failed creating bootstrap updater", &error))?;
        let digest = copy_exact(&mut entry, &mut output, signed.size)?;
        output
            .flush()
            .map_err(|error| format_io_error("failed flushing bootstrap updater", &error))?;
        if digest != parse_hash(&signed.sha256)? {
            return Err("bootstrap updater hash does not match signed manifest".into());
        }
        Ok(())
    })();
    if result.is_err() {
        let _ = fs::remove_file(destination);
    }
    result
}

struct AppliedFile {
    target: PathBuf,
    backup: Option<PathBuf>,
}

fn rollback(applied: &[AppliedFile]) -> Result<(), String> {
    let mut errors = Vec::new();
    for file in applied.iter().rev() {
        if let Err(error) = fs::remove_file(&file.target) {
            if error.kind() != io::ErrorKind::NotFound {
                errors.push(format_io_error(
                    &format!("failed removing {} during rollback", file.target.display()),
                    &error,
                ));
            }
        }
        if let Some(backup) = &file.backup {
            if let Err(error) = fs::rename(backup, &file.target) {
                errors.push(format_io_error(
                    &format!(
                        "failed restoring {} from {}",
                        file.target.display(),
                        backup.display()
                    ),
                    &error,
                ));
            }
        }
    }
    if errors.is_empty() {
        Ok(())
    } else {
        Err(errors.join("; "))
    }
}

#[derive(Debug)]
struct InstallFailure {
    message: String,
}

fn rollback_install(message: String, applied: &[AppliedFile], backup: &Path) -> InstallFailure {
    match rollback(applied) {
        Ok(()) => match fs::remove_dir_all(backup) {
            Ok(()) => InstallFailure { message },
            Err(error) if error.kind() == io::ErrorKind::NotFound => InstallFailure { message },
            Err(error) => InstallFailure {
                message: format!(
                    "{message}; rollback completed, but backup cleanup failed: {}; backup remains in {}",
                    format_io_error("failed removing rollback backup", &error),
                    backup.display()
                ),
            },
        },
        Err(error) => InstallFailure {
            message: format!(
                "{message}; rollback failed: {error}; recovery files remain in {}",
                backup.display()
            ),
        },
    }
}

fn install_staged(
    staging: &Path,
    backup: &Path,
    install: &Path,
    manifest: &Manifest,
) -> Result<(), InstallFailure> {
    let mut applied = Vec::new();
    for signed in &manifest.files {
        let relative = validate_relative_path(&signed.path)
            .map_err(|error| rollback_install(error, &applied, backup))?;
        validate_install_target(install, &relative)
            .map_err(|error| rollback_install(error, &applied, backup))?;
        let source = staging.join(&relative);
        let target = install.join(&relative);
        // storage.cfg 仍供服务端使用，更新保留既有文件；客户端存储由构建决定。
        if signed.path.eq_ignore_ascii_case("storage.cfg") && target.exists() {
            continue;
        }
        if let Some(parent) = target.parent() {
            if let Err(error) = fs::create_dir_all(parent) {
                return Err(rollback_install(
                    format_io_error("failed creating install directory", &error),
                    &applied,
                    backup,
                ));
            }
        }
        let backup_path = if target.exists() {
            let path = backup.join(&relative);
            if let Some(parent) = path.parent() {
                if let Err(error) = fs::create_dir_all(parent) {
                    return Err(rollback_install(
                        format_io_error("failed creating backup directory", &error),
                        &applied,
                        backup,
                    ));
                }
            }
            if let Err(error) = fs::rename(&target, &path) {
                return Err(rollback_install(
                    format_io_error(
                        &format!(
                            "failed backing up {} (close DDNet-Server.exe if it is running)",
                            signed.path
                        ),
                        &error,
                    ),
                    &applied,
                    backup,
                ));
            }
            Some(path)
        } else {
            None
        };
        applied.push(AppliedFile {
            target: target.clone(),
            backup: backup_path,
        });
        if let Err(error) = fs::rename(&source, &target) {
            return Err(rollback_install(
                format_io_error(&format!("failed installing {}", signed.path), &error),
                &applied,
                backup,
            ));
        }
    }
    Ok(())
}

fn apply_update(
    package_path: &Path,
    package_signature_path: &Path,
    manifest_path: &Path,
    manifest_signature_path: &Path,
    install_path: &Path,
    current_version: &str,
) -> Result<(), String> {
    apply_update_with_key(
        package_path,
        package_signature_path,
        manifest_path,
        manifest_signature_path,
        install_path,
        current_version,
        &PUBLIC_KEY,
    )
}

// 生产入口固定内嵌公钥，内部依赖注入允许测试覆盖完整验签与安装事务。
fn apply_update_with_key(
    package_path: &Path,
    package_signature_path: &Path,
    manifest_path: &Path,
    manifest_signature_path: &Path,
    install_path: &Path,
    current_version: &str,
    key: &[u8; 32],
) -> Result<(), String> {
    let manifest_bytes = read_limited(manifest_path, MAX_MANIFEST_SIZE)?;
    let manifest_signature = read_limited(manifest_signature_path, 64)?;
    verify_signature_with_key(&manifest_bytes, &manifest_signature, key)?;
    let manifest = parse_manifest(&manifest_bytes)?;
    validate_not_downgrade(&manifest.version, current_version)?;
    let package_signature = read_limited(package_signature_path, 64)?;
    validate_package_with_key(package_path, &package_signature, &manifest, key)?;
    validate_install_root(install_path)?;
    let staging = unique_child(install_path, "qm-update-staging")?;
    let backup = match unique_child(install_path, "qm-update-backup") {
        Ok(path) => path,
        Err(error) => {
            let _ = fs::remove_dir_all(&staging);
            return Err(error);
        }
    };
    if let Err(error) = extract_package(package_path, &staging, &manifest) {
        let _ = fs::remove_dir_all(&staging);
        let _ = fs::remove_dir_all(&backup);
        return Err(error);
    }
    let result = install_staged(&staging, &backup, install_path, &manifest);
    let _ = fs::remove_dir_all(&staging);
    match result {
        Ok(()) => fs::remove_dir_all(&backup)
            .map_err(|error| format_io_error("failed removing completed update backup", &error)),
        Err(error) => Err(error.message),
    }
}

unsafe fn input_bytes<'a>(data: *const u8, size: usize) -> Result<&'a [u8], String> {
    if size == 0 {
        return Ok(&[]);
    }
    if data.is_null() {
        return Err("null update input".into());
    }
    Ok(slice::from_raw_parts(data, size))
}

unsafe fn input_path<'a>(value: *const c_char) -> Result<&'a Path, String> {
    if value.is_null() {
        return Err("null update path".into());
    }
    let text = CStr::from_ptr(value)
        .to_str()
        .map_err(|_| "update path is not valid UTF-8")?;
    Ok(Path::new(text))
}

unsafe fn input_string<'a>(value: *const c_char) -> Result<&'a str, String> {
    if value.is_null() {
        return Err("null update string".into());
    }
    CStr::from_ptr(value)
        .to_str()
        .map_err(|_| "update string is not valid UTF-8".into())
}

unsafe fn write_error(error: &str, output: *mut c_char, output_size: usize) {
    if output.is_null() || output_size == 0 {
        return;
    }
    let bytes = error.as_bytes();
    let copy_size = bytes.len().min(output_size - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), output.cast(), copy_size);
    *output.add(copy_size) = 0;
}

/// Verifies and parses an in-memory update manifest and its detached signature.
pub unsafe fn ffi_verify_manifest(
    manifest: *const u8,
    manifest_size: usize,
    signature: *const u8,
    signature_size: usize,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    let result = input_bytes(manifest, manifest_size)
        .and_then(|bytes| input_bytes(signature, signature_size).map(|sig| (bytes, sig)))
        .and_then(|(bytes, sig)| verify_manifest(bytes, sig).map(|_| ()));
    if let Err(message) = &result {
        write_error(message, error, error_size);
    }
    result.is_ok()
}

/// Verifies a manifest signature and returns the signed package metadata.
pub unsafe fn ffi_verify_manifest_package(
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
    let result = input_bytes(manifest, manifest_size)
        .and_then(|bytes| input_bytes(signature, signature_size).map(|sig| (bytes, sig)))
        .and_then(|(bytes, sig)| verify_manifest(bytes, sig))
        .and_then(|manifest| {
            if package_size.is_null() || package_digest.is_null() || package_digest_size != 32 {
                return Err("invalid update package metadata output".into());
            }
            let digest = parse_hash(&manifest.package.sha256)?;
            *package_size = manifest.package.size;
            std::ptr::copy_nonoverlapping(digest.as_ptr(), package_digest, digest.len());
            Ok(())
        });
    if let Err(message) = &result {
        write_error(message, error, error_size);
    }
    result.is_ok()
}

// Setup 清单独立发布，旧客户端的 ZIP 清单格式保持兼容。
fn parse_setup_manifest(bytes: &[u8]) -> Result<Manifest, String> {
    if bytes.is_empty() || bytes.len() > MAX_MANIFEST_SIZE {
        return Err("setup manifest size is outside the supported range".into());
    }
    let manifest: Manifest = serde_json::from_slice(bytes)
        .map_err(|error| format!("invalid setup manifest: {error}"))?;
    validate_version(&manifest.version)?;
    if manifest.schema != 1
        || manifest.package.name != "QmClient-Setup.exe"
        || manifest.package.size == 0
        || manifest.package.size > MAX_PACKAGE_SIZE
        || !manifest.files.is_empty()
    {
        return Err("invalid setup package metadata".into());
    }
    parse_hash(&manifest.package.sha256)?;
    Ok(manifest)
}

fn verify_setup_manifest(
    bytes: &[u8],
    signature: &[u8],
    key: &[u8; 32],
) -> Result<Manifest, String> {
    verify_signature_with_key(bytes, signature, key)?;
    parse_setup_manifest(bytes)
}

/// 校验独立 Setup 清单，并返回签名覆盖的安装器大小与摘要。
pub unsafe fn ffi_verify_setup_manifest(
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
    let result = input_bytes(manifest, manifest_size)
        .and_then(|bytes| input_bytes(signature, signature_size).map(|sig| (bytes, sig)))
        .and_then(|(bytes, sig)| verify_setup_manifest(bytes, sig, &PUBLIC_KEY))
        .and_then(|manifest| {
            if package_size.is_null() || package_digest.is_null() || package_digest_size != 32 {
                return Err("invalid setup metadata output".into());
            }
            let digest = parse_hash(&manifest.package.sha256)?;
            *package_size = manifest.package.size;
            std::ptr::copy_nonoverlapping(digest.as_ptr(), package_digest, digest.len());
            Ok(())
        });
    if let Err(message) = &result {
        write_error(message, error, error_size);
    }
    result.is_ok()
}

/// 启动前重新校验磁盘上的 Setup、清单和签名。
pub unsafe fn ffi_verify_setup_files(
    package_path: *const c_char,
    package_signature_path: *const c_char,
    manifest_path: *const c_char,
    manifest_signature_path: *const c_char,
    current_version: *const c_char,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    let result = (|| {
        let package = input_path(package_path)?;
        let package_sig = read_limited(input_path(package_signature_path)?, 64)?;
        let bytes = read_limited(input_path(manifest_path)?, MAX_MANIFEST_SIZE)?;
        let sig = read_limited(input_path(manifest_signature_path)?, 64)?;
        let manifest = verify_setup_manifest(&bytes, &sig, &PUBLIC_KEY)?;
        validate_not_downgrade(&manifest.version, input_string(current_version)?)?;
        validate_package(package, &package_sig, &manifest)
    })();
    if let Err(message) = &result {
        write_error(message, error, error_size);
    }
    result.is_ok()
}

/// 独立安装器和客户端共用同一导出，避免只在 engine 包中接线。
#[no_mangle]
pub unsafe extern "C" fn qm_update_verify_setup_files(
    package_path: *const c_char,
    package_signature_path: *const c_char,
    manifest_path: *const c_char,
    manifest_signature_path: *const c_char,
    current_version: *const c_char,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    ffi_verify_setup_files(
        package_path,
        package_signature_path,
        manifest_path,
        manifest_signature_path,
        current_version,
        error,
        error_size,
    )
}

/// Verifies a detached package signature over the signed SHA-256 digest.
pub unsafe fn ffi_verify_package_digest(
    digest: *const u8,
    digest_size: usize,
    signature: *const u8,
    signature_size: usize,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    let result = input_bytes(digest, digest_size).and_then(|digest| {
        let digest: &[u8; 32] = digest
            .try_into()
            .map_err(|_| "invalid update package SHA-256 length")?;
        let signature = input_bytes(signature, signature_size)?;
        verify_signature_with_key(&package_signature_message(digest), signature, &PUBLIC_KEY)
    });
    if let Err(message) = &result {
        write_error(message, error, error_size);
    }
    result.is_ok()
}

/// Re-verifies the manifest and extracts its signed updater for first-time bootstrap.
pub unsafe fn ffi_extract_bootstrap_updater(
    package_path: *const c_char,
    manifest: *const u8,
    manifest_size: usize,
    signature: *const u8,
    signature_size: usize,
    destination: *const c_char,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    let result = input_path(package_path).and_then(|package| {
        input_bytes(manifest, manifest_size).and_then(|bytes| {
            input_bytes(signature, signature_size).and_then(|signature| {
                verify_manifest(bytes, signature).and_then(|manifest| {
                    input_path(destination).and_then(|destination| {
                        extract_bootstrap_updater(package, destination, &manifest)
                    })
                })
            })
        })
    });
    if let Err(message) = &result {
        write_error(message, error, error_size);
    }
    result.is_ok()
}

/// Re-verifies, extracts, and transactionally installs a signed update package.
#[no_mangle]
pub unsafe extern "C" fn qm_update_apply(
    package_path: *const c_char,
    package_signature_path: *const c_char,
    manifest_path: *const c_char,
    manifest_signature_path: *const c_char,
    install_path: *const c_char,
    current_version: *const c_char,
    error: *mut c_char,
    error_size: usize,
) -> bool {
    let result = input_path(package_path).and_then(|package| {
        input_path(package_signature_path).and_then(|package_signature| {
            input_path(manifest_path).and_then(|manifest| {
                input_path(manifest_signature_path).and_then(|manifest_signature| {
                    input_path(install_path).and_then(|install| {
                        input_string(current_version).and_then(|current_version| {
                            apply_update(
                                package,
                                package_signature,
                                manifest,
                                manifest_signature,
                                install,
                                current_version,
                            )
                        })
                    })
                })
            })
        })
    });
    if let Err(message) = &result {
        write_error(message, error, error_size);
    }
    result.is_ok()
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::{Signer, SigningKey};
    use std::io::Cursor;
    use tempfile::tempdir;
    use zip::write::FileOptions;

    const TEST_SEED: [u8; 32] = [7; 32];

    fn test_manifest(files: &[(&str, &[u8])]) -> Vec<u8> {
        let entries: Vec<_> = files
            .iter()
            .map(|(path, content)| {
                serde_json::json!({
                    "path": path,
                    "size": content.len(),
                    "sha256": format!("{:x}", Sha256::digest(content)),
                })
            })
            .collect();
        serde_json::to_vec(&serde_json::json!({
            "schema": 1,
            "version": "2.80.0",
            "package": {
                "name": "QmClient-windows.zip",
                "size": 1,
                "sha256": "00".repeat(32),
            },
            "files": entries,
        }))
        .unwrap()
    }

    #[test]
    fn portable_metadata_keeps_the_signed_package_variant() {
        let bytes = test_manifest(&[
            ("DDNet.exe", b"client"),
            ("DDNet-Server.exe", b"server"),
            ("QmClient-Updater.exe", b"updater"),
        ]);
        let mut value: serde_json::Value = serde_json::from_slice(&bytes).unwrap();
        value["package"]["name"] = serde_json::json!("QmClient-windows-portable.zip");
        let bytes = serde_json::to_vec(&value).unwrap();
        let manifest = parse_manifest(&bytes).unwrap();
        assert_eq!(manifest.package.name, "QmClient-windows-portable.zip");
        assert!(parse_setup_manifest(&bytes).is_err());
        value["package"]["name"] = serde_json::json!("unknown.zip");
        assert!(parse_manifest(&serde_json::to_vec(&value).unwrap()).is_err());
    }

    #[test]
    fn signed_setup_metadata_is_verified_and_tampering_is_rejected() {
        let key = SigningKey::from_bytes(&TEST_SEED);
        let bytes = serde_json::to_vec(&serde_json::json!({
            "schema": 1, "version": "3.4", "package": {
                "name": "QmClient-Setup.exe", "size": 12, "sha256": "ab".repeat(32)
            }, "files": []
        }))
        .unwrap();
        let sig = key.sign(&bytes).to_bytes();
        let result = verify_setup_manifest(&bytes, &sig, key.verifying_key().as_bytes()).unwrap();
        assert_eq!(result.package.size, 12);
        assert_eq!(parse_hash(&result.package.sha256).unwrap(), [0xab; 32]);
        let mut changed = bytes.clone();
        changed[0] ^= 1;
        assert!(verify_setup_manifest(&changed, &sig, key.verifying_key().as_bytes()).is_err());
        assert!(parse_manifest(&bytes).is_err());
    }

    #[test]
    fn setup_metadata_rejects_wrong_asset_invalid_hash_and_empty_package() {
        for (name, size, hash) in [
            ("other.exe", 12, "ab".repeat(32)),
            ("QmClient-Setup.exe", 0, "ab".repeat(32)),
            ("QmClient-Setup.exe", 12, "invalid".to_string()),
        ] {
            let bytes = serde_json::to_vec(&serde_json::json!({
                "schema": 1, "version": "3.4", "package": {
                    "name": name, "size": size, "sha256": hash
                }, "files": []
            }))
            .unwrap();
            assert!(
                parse_setup_manifest(&bytes).is_err(),
                "{name} {size} {hash}"
            );
        }
    }

    #[test]
    fn signature_verification_rejects_tampering() {
        let key = SigningKey::from_bytes(&TEST_SEED);
        let message = b"signed manifest";
        let signature = key.sign(message).to_bytes();
        assert!(
            verify_signature_with_key(message, &signature, key.verifying_key().as_bytes()).is_ok()
        );
        assert!(
            verify_signature_with_key(b"tampered", &signature, key.verifying_key().as_bytes())
                .is_err()
        );
    }

    #[test]
    fn signed_update_version_cannot_downgrade_installed_client() {
        assert!(validate_not_downgrade("2.79.31", "2.79.31").is_ok());
        assert!(validate_not_downgrade("2.80.0", "2.79.31").is_ok());
        assert!(validate_not_downgrade("2.79.30", "2.79.31").is_err());
        assert!(validate_not_downgrade("2.2147483647", "2.79.31").is_ok());
        assert!(validate_not_downgrade("2.2147483648", "2.79.31").is_err());
    }

    #[test]
    fn preview_order_and_formal_promotion_do_not_allow_downgrades() {
        assert!(validate_not_downgrade("3.3-preview.2", "3.3-preview.1").is_ok());
        assert!(validate_not_downgrade("3.3", "3.3-preview.99").is_ok());
        assert!(validate_not_downgrade("3.10", "3.9").is_ok());
        assert!(validate_not_downgrade("3.3-preview.1", "3.3-preview.2").is_err());
        assert!(validate_not_downgrade("3.3-preview.99", "3.3").is_err());
        assert!(validate_not_downgrade("3.3", "3.4-preview.1").is_err());
    }

    #[test]
    fn malformed_preview_versions_are_rejected_before_installation() {
        for version in [
            "3-preview.1",
            "3.3.0-preview.1",
            "3.3-preview.0",
            "3.3-preview.01",
            "3.3-preview.-1",
            "3.3-preview.2147483648",
            "3.3-preview.1junk",
        ] {
            assert!(validate_version(version).is_err(), "{version}");
        }
    }

    #[test]
    fn permission_errors_have_a_locale_independent_marker() {
        let error = io::Error::new(io::ErrorKind::PermissionDenied, "拒绝访问");
        let message = format_io_error("failed replacing update file", &error);
        assert!(message.starts_with(PERMISSION_DENIED_MARKER));
        assert!(message.contains("拒绝访问"));
    }

    #[test]
    fn update_io_works_on_a_small_worker_stack() {
        std::thread::Builder::new()
            .stack_size(256 * 1024)
            .spawn(|| {
                let input = vec![0x5a; 2 * 1024 * 1024];
                let (digest, size) = hash_reader(Cursor::new(&input)).unwrap();
                assert_eq!(size, input.len() as u64);
                assert_eq!(digest, Sha256::digest(&input).as_slice());

                let mut output = Vec::new();
                let copied_digest =
                    copy_exact(Cursor::new(&input), &mut output, input.len() as u64).unwrap();
                assert_eq!(output, input);
                assert_eq!(copied_digest, digest);
            })
            .unwrap()
            .join()
            .unwrap();
    }

    #[test]
    fn embedded_public_key_matches_release_signing_key() {
        let signature = [
            131, 198, 190, 48, 168, 213, 68, 28, 119, 97, 162, 223, 61, 212, 205, 190, 233, 3, 123,
            200, 111, 99, 60, 157, 233, 248, 179, 23, 101, 128, 18, 253, 32, 91, 13, 92, 251, 114,
            162, 56, 151, 179, 127, 18, 246, 233, 225, 5, 5, 16, 213, 240, 127, 122, 1, 132, 11,
            159, 125, 140, 43, 55, 7, 8,
        ];
        assert!(verify_signature_with_key(
            b"QmClient update signing key test",
            &signature,
            &PUBLIC_KEY,
        )
        .is_ok());
    }

    #[test]
    fn ffi_input_accepts_empty_slice_without_dereferencing_null() {
        assert!(unsafe { input_bytes(std::ptr::null(), 0) }
            .unwrap()
            .is_empty());
        assert!(unsafe { input_bytes(std::ptr::null(), 1) }.is_err());
    }

    #[test]
    fn standalone_setup_export_rejects_missing_paths_with_an_error() {
        let mut error = [0 as c_char; 128];
        assert!(!unsafe {
            qm_update_verify_setup_files(
                std::ptr::null(),
                std::ptr::null(),
                std::ptr::null(),
                std::ptr::null(),
                std::ptr::null(),
                error.as_mut_ptr(),
                error.len(),
            )
        });
        assert_eq!(
            unsafe { CStr::from_ptr(error.as_ptr()) }.to_str().unwrap(),
            "null update path"
        );
    }

    #[test]
    fn manifest_rejects_windows_path_traversal_and_reserved_names() {
        for path in [
            "../DDNet.exe",
            "data\\evil",
            "data/file:stream",
            "data/CON.txt",
            "data//file.txt",
            "data/file.",
            "data/file ",
            "profile/settings.cfg",
            "PROFILE/qmclient/settings.cfg",
        ] {
            assert!(validate_relative_path(path).is_err(), "accepted {path}");
        }
    }

    #[test]
    fn extraction_rejects_unsigned_zip_entries() {
        let required = [
            ("DDNet.exe", &b"client"[..]),
            ("DDNet-Server.exe", &b"server"[..]),
            ("QmClient-Updater.exe", &b"updater"[..]),
        ];
        let manifest = parse_manifest(&test_manifest(&required)).unwrap();
        let mut zip_bytes = Cursor::new(Vec::new());
        {
            let mut writer = zip::ZipWriter::new(&mut zip_bytes);
            writer
                .start_file("DDNet.exe", FileOptions::default())
                .unwrap();
            writer.write_all(b"client").unwrap();
            writer
                .start_file("evil.txt", FileOptions::default())
                .unwrap();
            writer.write_all(b"evil").unwrap();
            writer.finish().unwrap();
        }
        let root = tempdir().unwrap();
        let package = root.path().join("package.zip");
        fs::write(&package, zip_bytes.into_inner()).unwrap();
        assert!(extract_package(&package, root.path(), &manifest).is_err());
    }

    #[test]
    fn bootstrap_extracts_only_the_signed_updater() {
        let required = [
            ("DDNet.exe", &b"client"[..]),
            ("DDNet-Server.exe", &b"server"[..]),
            ("QmClient-Updater.exe", &b"updater"[..]),
        ];
        let manifest = parse_manifest(&test_manifest(&required)).unwrap();
        let mut zip_bytes = Cursor::new(Vec::new());
        {
            let mut writer = zip::ZipWriter::new(&mut zip_bytes);
            for (path, content) in required {
                writer.start_file(path, FileOptions::default()).unwrap();
                writer.write_all(content).unwrap();
            }
            writer.finish().unwrap();
        }
        let root = tempdir().unwrap();
        let package = root.path().join("package.zip");
        let updater = root.path().join("bootstrap.exe");
        fs::write(&package, zip_bytes.into_inner()).unwrap();
        extract_bootstrap_updater(&package, &updater, &manifest).unwrap();
        assert_eq!(fs::read(updater).unwrap(), b"updater");
    }

    fn sevenzip_fixture(root: &Path, files: &[(&str, &[u8])]) -> (PathBuf, Manifest) {
        let package = root.join("package.7z");
        let mut writer = sevenz_rust::SevenZWriter::create(&package).unwrap();
        for (name, bytes) in files {
            let mut entry = sevenz_rust::SevenZArchiveEntry::new();
            entry.name = (*name).into();
            writer
                .push_archive_entry(entry, Some(Cursor::new(*bytes)))
                .unwrap();
        }
        writer.finish().unwrap();
        let mut value: serde_json::Value = serde_json::from_slice(&test_manifest(files)).unwrap();
        value["package"]["name"] = serde_json::json!("QmClient-windows.7z");
        let manifest = parse_manifest(&serde_json::to_vec(&value).unwrap()).unwrap();
        (package, manifest)
    }

    struct SignedInstallFixture {
        root: tempfile::TempDir,
        package: PathBuf,
        signature: PathBuf,
        manifest: PathBuf,
        manifest_signature: PathBuf,
        install: PathBuf,
        key: SigningKey,
    }

    impl SignedInstallFixture {
        fn new(portable: bool) -> Self {
            let root = tempdir().unwrap();
            let files: &[(&str, &[u8])] = &[
                ("DDNet.exe", b"new client"),
                ("DDNet-Server.exe", b"new server"),
                ("QmClient-Updater.exe", b"new updater"),
                ("storage.cfg", b"default policy"),
            ];
            let (package, _) = sevenzip_fixture(root.path(), files);
            let bytes = fs::read(&package).unwrap();
            let digest: [u8; 32] = Sha256::digest(&bytes).into();
            let mut value: serde_json::Value =
                serde_json::from_slice(&test_manifest(files)).unwrap();
            value["version"] = serde_json::json!("3.4");
            value["package"] = serde_json::json!({
                "name": if portable { "QmClient-windows-portable.7z" } else { "QmClient-windows.7z" },
                "size": bytes.len(), "sha256": format!("{:x}", Sha256::digest(&bytes)),
            });
            let key = SigningKey::from_bytes(&TEST_SEED);
            let manifest = root.path().join("manifest.json");
            let manifest_signature = root.path().join("manifest.sig");
            let signature = root.path().join("package.sig");
            let manifest_bytes = serde_json::to_vec(&value).unwrap();
            fs::write(&manifest, &manifest_bytes).unwrap();
            fs::write(&manifest_signature, key.sign(&manifest_bytes).to_bytes()).unwrap();
            fs::write(
                &signature,
                key.sign(&package_signature_message(&digest)).to_bytes(),
            )
            .unwrap();
            let install = root.path().join("install");
            fs::create_dir_all(install.join("profile")).unwrap();
            fs::create_dir_all(install.join("data/qmclient")).unwrap();
            fs::write(
                install.join("data/qmclient/gui_logo.png"),
                b"installed logo",
            )
            .unwrap();
            fs::write(install.join("DDNet.exe"), b"old client").unwrap();
            fs::write(install.join("storage.cfg"), b"custom policy").unwrap();
            fs::write(install.join("profile/settings.cfg"), b"user settings").unwrap();
            Self {
                root,
                package,
                signature,
                manifest,
                manifest_signature,
                install,
                key,
            }
        }
        fn apply(&self) -> Result<(), String> {
            apply_update_with_key(
                &self.package,
                &self.signature,
                &self.manifest,
                &self.manifest_signature,
                &self.install,
                "3.3",
                self.key.verifying_key().as_bytes(),
            )
        }
        fn assert_old_data(&self) {
            assert_eq!(
                fs::read(self.install.join("DDNet.exe")).unwrap(),
                b"old client"
            );
            assert_eq!(
                fs::read(self.install.join("storage.cfg")).unwrap(),
                b"custom policy"
            );
            assert_eq!(
                fs::read(self.install.join("profile/settings.cfg")).unwrap(),
                b"user settings"
            );
        }
    }

    #[test]
    fn signed_sevenzip_install_preserves_user_data_for_normal_and_portable() {
        for portable in [false, true] {
            let fixture = SignedInstallFixture::new(portable);
            fixture.apply().unwrap();
            assert_eq!(
                fs::read(fixture.install.join("DDNet.exe")).unwrap(),
                b"new client"
            );
            assert_eq!(
                fs::read(fixture.install.join("DDNet-Server.exe")).unwrap(),
                b"new server"
            );
            assert_eq!(
                fs::read(fixture.install.join("storage.cfg")).unwrap(),
                b"custom policy"
            );
            assert_eq!(
                fs::read(fixture.install.join("profile/settings.cfg")).unwrap(),
                b"user settings"
            );
            assert!(!fs::read_dir(&fixture.install).unwrap().any(|entry| entry
                .unwrap()
                .file_name()
                .to_string_lossy()
                .starts_with(".qm-update")));
        }
    }

    #[test]
    fn tampered_sevenzip_package_does_not_change_installation() {
        let fixture = SignedInstallFixture::new(false);
        let mut bytes = fs::read(&fixture.package).unwrap();
        let end = bytes.len() - 1;
        bytes[end] ^= 1;
        fs::write(&fixture.package, bytes).unwrap();
        assert!(fixture.apply().unwrap_err().contains("SHA-256"));
        fixture.assert_old_data();
        assert_eq!(fs::read_dir(&fixture.install).unwrap().count(), 4);
    }

    #[test]
    fn invalid_manifest_or_package_signature_does_not_change_installation() {
        for manifest_signature in [true, false] {
            let fixture = SignedInstallFixture::new(true);
            fs::write(
                if manifest_signature {
                    &fixture.manifest_signature
                } else {
                    &fixture.signature
                },
                [0; 64],
            )
            .unwrap();
            assert!(fixture.apply().unwrap_err().contains("signature"));
            fixture.assert_old_data();
            assert_eq!(fs::read_dir(&fixture.install).unwrap().count(), 4);
        }
    }

    #[test]
    fn signed_sevenzip_transaction_restores_client_when_later_target_is_directory() {
        let fixture = SignedInstallFixture::new(false);
        fs::create_dir(fixture.install.join("DDNet-Server.exe")).unwrap();
        let result = fixture.apply();
        assert!(result.unwrap_err().contains("not a regular file"));
        fixture.assert_old_data();
        assert!(fixture.install.join("DDNet-Server.exe").is_dir());
        assert!(fixture.root.path().exists());
    }

    #[test]
    fn sevenzip_extracts_signed_files_and_bootstrap() {
        let root = tempdir().unwrap();
        let files: &[(&str, &[u8])] = &[
            ("DDNet.exe", b"client"),
            ("DDNet-Server.exe", b"server"),
            ("QmClient-Updater.exe", b"updater"),
            ("data/example.txt", b"asset"),
        ];
        let (package, manifest) = sevenzip_fixture(root.path(), files);
        let staging = root.path().join("staging");
        fs::create_dir(&staging).unwrap();
        extract_package(&package, &staging, &manifest).unwrap();
        assert_eq!(
            fs::read(staging.join("data/example.txt")).unwrap(),
            b"asset"
        );
        let bootstrap = root.path().join("bootstrap.exe");
        extract_bootstrap_updater(&package, &bootstrap, &manifest).unwrap();
        assert_eq!(fs::read(bootstrap).unwrap(), b"updater");
    }

    #[test]
    fn sevenzip_unsigned_file_and_hash_mismatch_reject_bootstrap() {
        let root = tempdir().unwrap();
        let files: &[(&str, &[u8])] = &[
            ("DDNet.exe", b"client"),
            ("DDNet-Server.exe", b"server"),
            ("QmClient-Updater.exe", b"updater"),
        ];
        let (package, mut manifest) = sevenzip_fixture(root.path(), files);
        let bootstrap = root.path().join("bootstrap.exe");
        manifest.files.retain(|entry| entry.path != "DDNet.exe");
        assert!(extract_bootstrap_updater(&package, &bootstrap, &manifest)
            .unwrap_err()
            .contains("unsigned"));
        assert!(!bootstrap.exists());
        let (_, mut manifest) = sevenzip_fixture(root.path(), files);
        manifest
            .files
            .iter_mut()
            .find(|entry| entry.path == "QmClient-Updater.exe")
            .unwrap()
            .sha256 = "00".repeat(32);
        assert!(extract_bootstrap_updater(&package, &bootstrap, &manifest)
            .unwrap_err()
            .contains("hash mismatch"));
        assert!(!bootstrap.exists());
    }

    #[test]
    fn sevenzip_missing_signed_file_rejects_package() {
        let root = tempdir().unwrap();
        let files: &[(&str, &[u8])] = &[
            ("DDNet.exe", b"client"),
            ("DDNet-Server.exe", b"server"),
            ("QmClient-Updater.exe", b"updater"),
        ];
        let (package, mut manifest) = sevenzip_fixture(root.path(), files);
        manifest.files.push(ManifestFile {
            path: "missing.txt".into(),
            size: 1,
            sha256: "00".repeat(32),
        });
        let staging = root.path().join("staging");
        fs::create_dir(&staging).unwrap();
        assert!(extract_package(&package, &staging, &manifest)
            .unwrap_err()
            .contains("every file"));
    }

    #[test]
    fn zip_upgrade_preserves_server_config_and_portable_profile() {
        let root = tempdir().unwrap();
        let staging = root.path().join("staging");
        let backup = root.path().join("backup");
        let install = root.path().join("install");
        fs::create_dir_all(&staging).unwrap();
        fs::create_dir_all(&backup).unwrap();
        fs::create_dir_all(install.join("profile")).unwrap();
        fs::write(staging.join("storage.cfg"), b"server default").unwrap();
        fs::write(staging.join("DDNet.exe"), b"new portable client").unwrap();
        fs::write(install.join("DDNet.exe"), b"old portable client").unwrap();
        fs::write(install.join("storage.cfg"), b"custom user policy").unwrap();
        fs::write(install.join("profile/settings.cfg"), b"user settings").unwrap();
        let manifest = Manifest {
            schema: 1,
            version: "3.4".into(),
            package: Package {
                name: "QmClient-windows.zip".into(),
                size: 1,
                sha256: "00".repeat(32),
            },
            files: vec![
                ManifestFile {
                    path: "storage.cfg".into(),
                    size: 14,
                    sha256: "00".repeat(32),
                },
                ManifestFile {
                    path: "DDNet.exe".into(),
                    size: 19,
                    sha256: "00".repeat(32),
                },
            ],
        };
        assert!(install_staged(&staging, &backup, &install, &manifest).is_ok());
        assert_eq!(
            fs::read(install.join("storage.cfg")).unwrap(),
            b"custom user policy"
        );
        assert_eq!(
            fs::read(install.join("profile/settings.cfg")).unwrap(),
            b"user settings"
        );
        assert_eq!(
            fs::read(install.join("DDNet.exe")).unwrap(),
            b"new portable client"
        );
        fs::write(staging.join("DDNet.exe"), b"next portable client").unwrap();
        fs::remove_file(install.join("storage.cfg")).unwrap();
        assert!(install_staged(&staging, &backup, &install, &manifest).is_ok());
        assert_eq!(
            fs::read(install.join("storage.cfg")).unwrap(),
            b"server default"
        );
    }

    #[test]
    fn failed_install_rolls_back_previously_replaced_files() {
        let root = tempdir().unwrap();
        let staging = root.path().join("staging");
        let backup = root.path().join("backup");
        let install = root.path().join("install");
        fs::create_dir_all(&staging).unwrap();
        fs::create_dir_all(&backup).unwrap();
        fs::create_dir_all(install.join("data")).unwrap();
        fs::write(install.join("DDNet.exe"), b"client").unwrap();
        fs::create_dir_all(install.join("data/qmclient")).unwrap();
        fs::write(install.join("data/qmclient/gui_logo.png"), b"logo").unwrap();
        fs::write(staging.join("DDNet.exe"), b"new client").unwrap();
        fs::write(install.join("DDNet.exe"), b"old client").unwrap();
        fs::create_dir(install.join("blocked")).unwrap();
        let manifest = Manifest {
            schema: 1,
            version: "2.80.0".into(),
            package: Package {
                name: "QmClient-windows.zip".into(),
                size: 1,
                sha256: "00".repeat(32),
            },
            files: vec![
                ManifestFile {
                    path: "DDNet.exe".into(),
                    size: 10,
                    sha256: "00".repeat(32),
                },
                ManifestFile {
                    path: "blocked".into(),
                    size: 1,
                    sha256: "00".repeat(32),
                },
            ],
        };
        assert!(install_staged(&staging, &backup, &install, &manifest).is_err());
        assert_eq!(fs::read(install.join("DDNet.exe")).unwrap(), b"old client");
        assert!(!backup.exists());
    }

    #[test]
    fn install_target_validation_rejects_directory_in_place_of_file() {
        let root = tempdir().unwrap();
        let install = root.path().join("install");
        fs::create_dir_all(install.join("data/file.txt")).unwrap();
        let error = validate_install_target(&install, Path::new("data/file.txt")).unwrap_err();
        assert!(error.contains("not a regular file"));
    }

    #[test]
    fn install_root_requires_existing_qmclient_layout() {
        let root = tempdir().unwrap();
        let install = root.path().join("install");
        fs::create_dir_all(install.join("data/qmclient")).unwrap();
        assert!(validate_install_root(&install).is_err());
        fs::write(install.join("DDNet.exe"), b"client").unwrap();
        assert!(validate_install_root(&install).is_err());
        fs::write(install.join("data/qmclient/gui_logo.png"), b"logo").unwrap();
        assert!(validate_install_root(&install).is_ok());
    }

    #[cfg(unix)]
    #[test]
    fn install_target_validation_rejects_symbolic_link_parent() {
        use std::os::unix::fs::symlink;

        let root = tempdir().unwrap();
        let install = root.path().join("install");
        let outside = root.path().join("outside");
        fs::create_dir_all(&install).unwrap();
        fs::create_dir_all(&outside).unwrap();
        symlink(&outside, install.join("data")).unwrap();
        let error = validate_install_target(&install, Path::new("data/file.txt")).unwrap_err();
        assert!(error.contains("symbolic link or reparse point"));
    }

    #[test]
    fn failed_rollback_preserves_backup_for_manual_recovery() {
        let root = tempdir().unwrap();
        let backup = root.path().join("backup");
        let target = root.path().join("target-directory");
        fs::create_dir(&backup).unwrap();
        fs::create_dir(&target).unwrap();
        let failure = rollback_install(
            "installation failed".into(),
            &[AppliedFile {
                target,
                backup: None,
            }],
            &backup,
        );
        assert!(failure.message.contains("rollback failed"));
        assert!(failure.message.contains(&backup.display().to_string()));
        assert!(backup.exists());
    }

    #[test]
    fn full_package_extracts_and_installs_signed_file_set() {
        let required = [
            ("DDNet.exe", &b"new client"[..]),
            ("DDNet-Server.exe", &b"new server"[..]),
            ("QmClient-Updater.exe", &b"new updater"[..]),
            ("data/languages/test.txt", &b"translated"[..]),
        ];
        let manifest = parse_manifest(&test_manifest(&required)).unwrap();
        let mut zip_bytes = Cursor::new(Vec::new());
        {
            let mut writer = zip::ZipWriter::new(&mut zip_bytes);
            for (path, content) in required {
                writer.start_file(path, FileOptions::default()).unwrap();
                writer.write_all(content).unwrap();
            }
            writer.finish().unwrap();
        }
        let root = tempdir().unwrap();
        let package = root.path().join("package.zip");
        let staging = root.path().join("staging");
        let backup = root.path().join("backup");
        let install = root.path().join("install");
        fs::write(&package, zip_bytes.into_inner()).unwrap();
        fs::create_dir_all(&staging).unwrap();
        fs::create_dir_all(&backup).unwrap();
        fs::create_dir_all(&install).unwrap();
        fs::write(install.join("DDNet.exe"), b"old client").unwrap();

        extract_package(&package, &staging, &manifest).unwrap();
        install_staged(&staging, &backup, &install, &manifest).unwrap();

        for (path, content) in required {
            assert_eq!(fs::read(install.join(path)).unwrap(), content);
        }
        assert_eq!(fs::read(backup.join("DDNet.exe")).unwrap(), b"old client");
    }
}
