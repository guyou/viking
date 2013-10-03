#include <babel.h>

void print_file_format (BabelFile *file, gconstpointer user_data)
{
	printf("%s : %d%d%d%d%d%d\n",
			file->label,
			file->mode.waypointsRead, file->mode.waypointsWrite,
			file->mode.tracksRead, file->mode.tracksWrite,
			file->mode.routesRead, file->mode.routesWrite);
}

int main(int argc, char*argv[])
{
	a_babel_init();

	BabelMode mode = { 0,0,1,0,1,0 };
	a_babel_foreach_file_with_mode(mode, print_file_format, NULL);

	a_babel_uninit();
}
