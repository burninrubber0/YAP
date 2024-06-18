#include <yap.h>
#include <QScopedPointer>

int main(int argc, char* argv[])
{
	QScopedPointer<YAP> app(new YAP(argc, argv));
	return app->result;
}
