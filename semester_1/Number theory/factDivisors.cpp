#include <iostream>
#include <cmath>
#include <vector>
#include <print>
using namespace std;
bool is_prost(int N)
{
int koren=sqrt(N);
for (int i=2;i<=koren;i++) if (N%i==0) return false;
return true;
}
int count_div_i(int N,int i)
{
int count_div=0;
while (N>0) {N=N/i;count_div+=N;}
return count_div;
}
int main()
{
    vector<int> prostlist;
    int N;
    print("Enter N");
    cin>>N;
    prostlist.push_back(2);
    for (int i=3;i<=N;i+=2) if (is_prost(i)) prostlist.push_back(i);

    for (int i=0;i<prostlist.size();i++)
        print("{}   {}\n",prostlist[i],count_div_i(N,prostlist[i]));
    return 0;
}
