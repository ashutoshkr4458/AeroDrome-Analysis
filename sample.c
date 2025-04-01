
#include<stdio.h>
#include<fcntl.h>
#include<pthread.h>

pthread_mutex_t lock;
pthread_t t1,t2;

int glo1, glo2;

void write(int * a){
	*a=0;
}

void global_vars(){
	pthread_mutex_lock(&lock);
	pthread_mutex_unlock(&lock);
	write(&glo1);
	write(&glo2);
}

void* txn1(void* arg){
	glo1++;
	glo2++;
	return NULL;
}

void* txn2(void* arg){
	
	txn1(NULL);
	glo1-=2;
	glo2*=5;
	return NULL;
}

void* thrd1(void* arg){
	txn1(0);
	txn2(0);
	
}

void* thrd2(void* arg){
	txn2(0);
}

int main(){

	global_vars();
	//txn1();
	//txn2();		
	
	 
	pthread_create(&t1, NULL, thrd1, NULL);

	pthread_create(&t2, NULL, thrd2, NULL);

	pthread_join(t1, NULL);
	pthread_join(t1, NULL);
	
	//cout<<hex<<&lock<<" "<<&glo1<<" "<<&glo2<<endl;
	printf("%p %p %p\n", &lock, &glo1, &glo2);
    return 0;
}
