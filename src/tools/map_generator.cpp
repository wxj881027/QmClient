#include "map_generator/generator.h"

#include <base/logger.h>
#include <base/system.h>

int main(int argc, const char **argv)
{
	CCmdlineFix CmdlineFix(&argc, &argv);
	log_set_global_logger_default();
	return RunMapGenerator(argc, argv);
}
