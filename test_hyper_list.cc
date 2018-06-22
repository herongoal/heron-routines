#include <iostream>
#include <cstring>
#include <sys/time.h>

using namespace std;

int main()
{
	struct timeval ts;
	struct timeval te;
	char	*ps = new char[1024];
	char	*pd = new char[1024];
	memcpy(pd, ps, 1024);

	gettimeofday(&ts, NULL);
	for(int n = 0; n < 100000000; ++n)
	{
		memcpy(pd, ps, 1024);
	}
	gettimeofday(&te, NULL);
	cout << te.tv_sec - ts.tv_sec << endl;
	cout << te.tv_usec - ts.tv_usec << endl;

}
