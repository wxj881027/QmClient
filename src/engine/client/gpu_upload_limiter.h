/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_GPU_UPLOAD_LIMITER_H
#define ENGINE_CLIENT_GPU_UPLOAD_LIMITER_H

/**
 * Global GPU texture upload rate limiter.
 *
 * This class provides frame-based throttling for GPU texture uploads
 * to prevent frame stuttering caused by batch texture uploads.
 *
 * Usage:
 * - Call OnFrameStart() at the beginning of each frame
 * - Call CanUpload() before each texture upload
 * - Call OnUploaded() after each successful texture upload
 *
 * Thread Safety:
 * - This class is designed for single-threaded use on the main/render thread
 * - No locking is required as it's only accessed from the main thread
 */
class CGpuUploadLimiter
{
public:
	/**
	 * Maximum number of texture uploads allowed per frame.
	 * This value is tuned to balance loading speed vs frame smoothness.
	 */
	static constexpr int MAX_UPLOADS_PER_FRAME = 30;

	/**
	 * Reset the upload counter for a new frame.
	 * Should be called at the beginning of each frame in OnRender().
	 */
	void OnFrameStart() { m_UploadsThisFrame = 0; }

	/**
	 * Check if a new texture upload is allowed this frame.
	 * @return true if upload is allowed, false if the limit has been reached
	 */
	bool CanUpload() const { return m_UploadsThisFrame < MAX_UPLOADS_PER_FRAME; }

	/**
	 * Record that a texture upload has been performed.
	 * Should be called after each successful texture upload.
	 */
	void OnUploaded() { ++m_UploadsThisFrame; }

	/**
	 * Get the number of uploads performed this frame.
	 * Useful for debugging and statistics.
	 */
	int UploadsThisFrame() const { return m_UploadsThisFrame; }

	/**
	 * Get the maximum uploads per frame limit.
	 */
	static constexpr int MaxUploadsPerFrame() { return MAX_UPLOADS_PER_FRAME; }

private:
	int m_UploadsThisFrame = 0;
};

#endif // ENGINE_CLIENT_GPU_UPLOAD_LIMITER_H
