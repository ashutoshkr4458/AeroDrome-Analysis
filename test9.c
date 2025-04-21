#include<stdio.h>
#include<fcntl.h>
#include<pthread.h>
#include<unistd.h>

int axyz,b,c,d;
pthread_mutex_t lck1, lck2;

void write_(int* a, int val){
	*a = val;
}


void global_vars(){
        pthread_mutex_lock(&lck1);
        pthread_mutex_unlock(&lck1);
        pthread_mutex_lock(&lck2);
        pthread_mutex_unlock(&lck2);
        write_(&axyz, 0);
        write_(&b, 1);
        write_(&c, 0);
        write_(&d, 0);
}

void* txn1(void* arg){
	sleep(2);
	int  a = axyz;
	sleep(6);
	axyz++;
	return NULL;
}

void* txn2(void* arg){
	sleep(2);
	int a = b;
	sleep(4);
	b++;
	sleep(4);
	a = axyz;
	axyz++;
	return NULL;
}

void* txn3(void* arg){
	
	sleep(2);
	int a = b;
	sleep(6);
	axyz++;
	return NULL;
}

void* thrd2(void* arg){
	txn2(0);
}

void* thrd3(void* arg){
	txn3(0);
}
	
void* thrd1(void* arg){
	txn1(0);
}



int main(){
	
	printf("vars %p %p %p\n", &axyz, &b, &lck1); 
	global_vars();
	pthread_t t1,t2,t3;
	//pthread_mutex_lock(&lck1);
	//pthread_mutex_unlock(&lck1);
	pthread_create(&t1, NULL, thrd1, NULL);
	pthread_create(&t2, NULL, thrd2, NULL);
	pthread_create(&t3, NULL, thrd3, NULL);
	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	pthread_join(t3, NULL);
	printf("%d\n", axyz);

    return 0;
}
