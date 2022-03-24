#include <iostream>
using namespace std;

int main(void)
{
    cout << "test start" << endl;

	//new int & delete int
    int *pnum = new int;
    *pnum = 3;
    cout << "ptr: " << pnum << "\tvalue: " << *pnum <<endl;
    cout << "heap size: " << sizeof(*pnum) << endl;
    delete pnum;
	
	//new int[] & delete[] int
    int num = 5;
    int *arr = new int[num];
    for(int i = 0; i < num ; i++)
    {
        arr[i] = i;
        cout << "ptr: " << &(arr[i]) << "\tvalue: " << arr[i] <<endl;
    }
    delete[] arr;

	//new int and doule with init
    pnum = new int(10);
    cout << "ptr: " << pnum << "\tvalue: " << *pnum <<endl;
    delete pnum;

    double *pdouble{new double[5]};
    cout << "ptr: " << pdouble <<endl;
    delete[] pdouble;

    cout << "test done" << endl;
    return 0;
}
