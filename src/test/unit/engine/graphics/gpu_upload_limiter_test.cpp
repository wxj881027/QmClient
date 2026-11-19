#include <engine/client/gpu_upload_limiter.h>

#include <gtest/gtest.h>

#include <initializer_list>

TEST(GpuUploadLimiter, InitialFrameUsesDefaultAllowance)
{
	CGpuUploadLimiter Limiter;
	EXPECT_EQ(Limiter.UploadsThisFrame(), 0);
	EXPECT_EQ(Limiter.RemainingUploads(), CGpuUploadLimiter::DEFAULT_MAX_UPLOADS_PER_FRAME);
	EXPECT_TRUE(Limiter.CanUpload(CGpuUploadLimiter::DEFAULT_MAX_UPLOADS_PER_FRAME));
	EXPECT_FALSE(Limiter.CanUpload(CGpuUploadLimiter::DEFAULT_MAX_UPLOADS_PER_FRAME + 1));
}

TEST(GpuUploadLimiter, BatchCheckDoesNotReserveAndCommittedUploadsReduceAllowance)
{
	CGpuUploadLimiter Limiter;
	Limiter.OnFrameStart(4);
	EXPECT_TRUE(Limiter.CanUpload(2));
	EXPECT_TRUE(Limiter.CanUpload(2));
	EXPECT_EQ(Limiter.UploadsThisFrame(), 0);
	Limiter.OnUploaded();
	Limiter.OnUploaded();
	Limiter.OnUploaded();
	EXPECT_FALSE(Limiter.CanUpload(2));
	EXPECT_TRUE(Limiter.CanUpload());
	EXPECT_EQ(Limiter.RemainingUploads(), 1);
	Limiter.OnUploaded();
	EXPECT_FALSE(Limiter.CanUpload());
	EXPECT_EQ(Limiter.RemainingUploads(), 0);
}

TEST(GpuUploadLimiter, NonpositiveFrameAllowanceBlocksUploads)
{
	for(int Limit : {0, -1})
	{
		SCOPED_TRACE(Limit);
		CGpuUploadLimiter Limiter;
		Limiter.OnFrameStart(Limit);
		EXPECT_FALSE(Limiter.CanUpload());
		EXPECT_FALSE(Limiter.CanUpload(2));
		EXPECT_TRUE(Limiter.CanUpload(0));
		EXPECT_EQ(Limiter.RemainingUploads(), 0);
	}
}

TEST(GpuUploadLimiter, NegativeBatchCannotBeAdmittedOrIncreaseAllowance)
{
	CGpuUploadLimiter Limiter;
	Limiter.OnFrameStart(2);
	EXPECT_FALSE(Limiter.CanUpload(-1));
	Limiter.OnUploaded();
	EXPECT_FALSE(Limiter.CanUpload(-1));
	EXPECT_EQ(Limiter.RemainingUploads(), 1);
}

TEST(GpuUploadLimiter, NewFrameRestoresAllowanceAfterExhaustion)
{
	CGpuUploadLimiter Limiter;
	Limiter.OnFrameStart(2);
	Limiter.OnUploaded();
	Limiter.OnUploaded();
	ASSERT_FALSE(Limiter.CanUpload());
	Limiter.OnFrameStart(3);
	EXPECT_EQ(Limiter.UploadsThisFrame(), 0);
	EXPECT_EQ(Limiter.RemainingUploads(), 3);
	EXPECT_TRUE(Limiter.CanUpload(3));
}
