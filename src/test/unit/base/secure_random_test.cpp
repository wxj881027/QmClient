// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/detect.h>
#include <base/secure.h>

#include <gtest/gtest.h>
#include <test/test.h>

#if GTEST_HAS_DEATH_TEST && defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

TEST(SecureRandomDeathTest, SourceDescriptorIsCloseOnExec)
{
	ASSERT_EXIT(
		{
			alarm(3);
			const int Descriptor = open("/dev/urandom", O_RDONLY);
			if(Descriptor == -1)
				_exit(2);
			struct stat Expected{};
			const bool KnownSource = fstat(Descriptor, &Expected) == 0;
			close(Descriptor);
			if(!KnownSource)
				_exit(2);

			// 在重新执行的测试进程中首次初始化，随机源会占用最小的空闲描述符。
			unsigned Random;
			secure_random_fill(&Random, sizeof(Random));
			struct stat Actual{};
			const int Flags = fcntl(Descriptor, F_GETFD);
			const bool IsSource = fstat(Descriptor, &Actual) == 0 && Actual.st_dev == Expected.st_dev && Actual.st_ino == Expected.st_ino;
			_exit(IsSource && Flags != -1 && (Flags & FD_CLOEXEC) != 0 ? 0 : 3);
		},
		testing::ExitedWithCode(0), "");
}
#endif

TEST(SecureRandom, Fill)
{
	unsigned int Bits = 0;
	while(~Bits)
	{
		unsigned int Random;
		secure_random_fill(&Random, sizeof(Random));
		Bits |= Random;
	}
}

TEST(SecureRandom, Below1)
{
	EXPECT_EQ(secure_rand_below(1), 0);
}

TEST(SecureRandom, Below)
{
	const int BOUNDS[] = {2, 3, 4, 5, 10, 100, 127, 128, 129};
	for(auto Below : BOUNDS)
	{
		for(int j = 0; j < 10; j++)
		{
			int Random = secure_rand_below(Below);
			EXPECT_GE(Random, 0);
			EXPECT_LT(Random, Below);
		}
	}
}
