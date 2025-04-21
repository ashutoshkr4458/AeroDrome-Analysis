/*
 * Copyright (C) 2004-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
// This tool counts the number of times a routine is executed and
// the number of instructions executed in a routine
//

#include<unordered_map>
#include<map>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string.h>
#include<set>
#include "pin.H"
using std::cerr;
using std::dec;
using std::endl;
using std::hex;
using std::ofstream;
using std::setw;
using std::string;
using namespace std;

ofstream outFile;

bool start=false;
unordered_map <ADDRINT, string> mp;

long long thrd = 16; //defined through flags
long long locks; //defined through 
long long vars;


PIN_LOCK pinl;

map<ADDRINT, PIN_LOCK> mpinl;

map<ADDRINT,long long> lck, var; //maps number to lock addresses
map<long long, long long> thrdid;   //maps os thrdid to pin tid  

map<long long, long long> lastrel;
// locks get numbered in the order they appear
set<ADDRINT> lock_addr, var_addr;
map <long long, map<long long ,long long>> C, Ct, L;

map<long long, map<long long, long long>> wrt;
map<long long, map<long long, map<long long, long long>>> rd;
map<long long, long long> lastwrt;

map<long long, long long> act_txn; //keeps count of active transactions

bool glob=false;

// Holds instruction count for a single procedure
typedef struct RtnCount
{
    string _name;
    string _image;
    ADDRINT _address;
    RTN _rtn;
    UINT64 _rtnCount;
    UINT64 _icount;
    struct RtnCount* _next;
} RTN_COUNT;

// Linked list of instruction counts for each routine
RTN_COUNT* RtnList = 0;

// This function is called before every instruction is executed
VOID docount(UINT64* counter) { (*counter)++; }

const char* StripPath(const char* path)
{
    const char* file = strrchr(path, '/');
    if (file)
        return file + 1;
    else
        return path;
}


bool clkComp(map<long long, long long> clk1,map<long long, long long> clk2 ){
    bool ans=true;
    for(int i=0;i<=thrd;i++){
      if(!(clk1[i] <= clk2[i])){
        ans=false;
        break;
      }
    }
    return ans;
}

map<long long, long long> joinVC(map<long long, long long> vc1, map<long long, long long> vc2){
    map<long long, long long> ans;
    for(int i=0;i<=thrd;i++){
      ans[i] = max(vc1[i], vc2[i]);
    }
    return ans;
}

void checkAndGet(map<long long, long long> clk, long long t){
    if(clkComp(Ct[t], clk) && act_txn[t]>0/* i.e. t has active transaction*/){
      outFile<<"Conflict serialisability violation\n";
      exit(0);
    }
  
    C[t] = joinVC(C[t],clk);
}

void acquire(long long t, long long l){
    if(lastrel[l] != t){
      checkAndGet(L[l], t);
    }
}

void release(long long t, long long l){
    L[l] =  C[t];
    lastrel[l]=t;
}

void fork(long long t, long long u){
    C[u] = joinVC(C[u], C[t]);
}

void join(long long t, long long u){
    checkAndGet(C[u], t);
}

void read(long long t, long long x){
    if(lastwrt[x]!=t){
        checkAndGet(wrt[x],t);
    }
    rd[t][x] = C[t];
}

void write(long long t, long long x){
    if(lastwrt[x]!=t){
        checkAndGet(wrt[x], t);
    }
    for(int u=0;u<=thrd;u++){
        if(u!=t){
        checkAndGet(rd[u][x], t);
        }
    }

    wrt[x]=C[t];
    lastwrt[x]=t;
}

void begin(long long t){
    C[t][t] = C[t][t]+1;
    Ct[t]=C[t];
    act_txn[t]++;
}

void end(long long t){
    
    act_txn[t]--;
    for(int u=0;u<=thrd;u++){
        if(u!=t){
        if(clkComp(Ct[t], C[u])){
            checkAndGet(C[t], u);
        }
        }
    }

    for(int l=0;l<locks;l++){
        if(clkComp(Ct[t], L[l])){
        L[l] = joinVC(C[t], L[l]);
        }
    }

    for(int x=0;x<=vars;x++){
        if(clkComp(Ct[t],wrt[x])){
        wrt[x]=joinVC(C[t], wrt[x]);
        }
        for(int u=0;u<=thrd;u++){
        if(clkComp(Ct[t],rd[u][x])){
            rd[u][x] = joinVC(C[t],rd[u][x]);
        }
        }
    }

}


void initialisation(){
    //clock init
    for(int i=0;i<=thrd;i++){
      for(int j=0;j<=thrd;j++){
        C[i][j]=0;
        Ct[i][j]=0;
      }
      C[i][i]=1;
    }

    for(int i=0;i<=thrd;i++){
        act_txn[i]=0; //keep incrementing at each nested txn
    }

    int cnt=0;
    for(auto it:lock_addr){
        lck[it] = cnt;
        cnt++;
    }
    cnt=0;
    for(auto it:var_addr){
        //initialising locks for all variables
        PIN_InitLock(&mpinl[it]);

        var[it]=cnt;
        cnt++;
    }
  
    for(int i=0;i<locks;i++){
      for(int j=0;j<=thrd;j++){
        L[i][j]=0;
      }
      lastrel[i]=-1;
    }
  
    for(int i=0;i<vars;i++){
      for(int j=0;j<=thrd;j++){
        wrt[i][j]=0;
      }
      lastwrt[i]=-1;
      
      for(int t=0;t<=thrd;t++){
        for(int j=0;j<=thrd;j++){
          rd[t][i][j]=0;
        }
      }
    }

  
}


VOID checkmain(const char* str){
	if(strlen(str)>=4 && (str[0]=='m' && str[1]=='a' && str[2]=='i' && str[3]=='n')){
		start=true;
        long long p = PIN_GetTid();
        thrdid[p]=0;

	}

   

    if(strcmp(str, "global_vars")==0){
        glob=true;

    }

	return;
}

VOID beginfunc(char* func, THREADID tid, ADDRINT arg0, ADDRINT arg1){
    if(start){
        if(glob){
            if(strcmp(func,"write_")==0){
                //arg0 is a global var
                var_addr.insert(arg0);
            }

            if(strcmp(func, "pthread_mutex_lock")==0){
                //arg0 is lock address
                lock_addr.insert(arg0);
            }
        }else{
            if(strncmp(func, "thrd", 4)==0){
                //thread begin

                long long par_tid = PIN_GetParentTid();
                long long cur_tid = PIN_GetTid();
                if(thrdid.find(par_tid)==thrdid.end() && par_tid!=0){
                     outFile<<"Parent tid not init\n";
                     outFile<<par_tid<<" "<<cur_tid<<endl;
                    exit(0);
                }
                if(par_tid!=0){
                    if(thrdid.find(cur_tid)==thrdid.end()){
                        thrdid[cur_tid] = thrdid.size();
                    }

                    fork(thrdid[par_tid], thrdid[cur_tid]);
                }
            }

            

            if(strncmp(func, "txn", 3)==0){
                begin(thrdid[PIN_GetTid()]);
            }


        }
    }

    
	return;
}
VOID retfunc(const char* func, THREADID tid, ADDRINT arg0, ADDRINT arg1){
	
    if(strncmp(func,"main",4)==0){
        start=false;
    }
    
    if(strcmp(func, "global_vars")==0){
        glob=false;
	
        //initialise everything here
        locks = lock_addr.size();
        vars = var_addr.size();
        initialisation();

    }

    if(start && !glob){
        if(strncmp(func,"thrd",4)==0){

            if(thrdid.find(PIN_GetParentTid())==thrdid.end() && PIN_GetParentTid()!=0){
                outFile<<"Error in join ptid\n";
                exit(0);
            }
            if(thrdid.find(PIN_GetTid())==thrdid.end()){
                outFile<<"Error in join tid\n";
                exit(0);
            }

            if(PIN_GetParentTid()!=0)
            join(thrdid[PIN_GetParentTid()], thrdid[PIN_GetTid()]);
            
        }

        if(strcmp(func, "pthread_mutex_lock")==0){
            //lock acquire
            if(lock_addr.count(arg0)==1){
                // printf("pthread_mutex_lock ret %#lx\n", arg0);
                acquire(thrdid[PIN_GetTid()], lck[arg0]);
            }
            
        }

        if(strcmp(func, "pthread_mutex_unlock")==0){
            //lock acquire
            if(lock_addr.count(arg0)==1){
                printf("pthread_mutex_unlock ret %#lx\n", arg0);
                release(thrdid[PIN_GetTid()], lck[arg0]);
            }
             
        }

        if(strncmp(func, "txn", 3)==0){
            
            printf("%s ret t-%d\n", func, PIN_GetTid());
            end(thrdid[PIN_GetTid()]);
        }
    }
	return;
}

VOID Initialise(){
	outFile<<"This is initialisation"<<endl;
}

VOID RecordMemRead(THREADID tid, ADDRINT addr, UINT32 size) {

    if(start && !glob){
        if(var_addr.count(addr)){
            PIN_GetLock(&mpinl[addr], tid);
            read(tid, var[addr]);
            PIN_ReleaseLock(&mpinl[addr]);
        }
    }

}

VOID RecordMemWrite(THREADID tid, ADDRINT addr, UINT32 size) {

    if(start && !glob){
        if(var_addr.count(addr)){
            PIN_GetLock(&mpinl[addr], tid);
            write(tid, var[addr]);
            PIN_ReleaseLock(&mpinl[addr]);
        }
    }
    
}

VOID RecordNonStackRead(THREADID tid, ADDRINT addr) {
    if(start)
    std::cout << "[Thread " << tid << "] READ from non-stack address: " << std::hex << addr << std::endl;
}

VOID RecordNonStackWrite(THREADID tid, ADDRINT addr) {
    if(start)
    std::cout << "[Thread " << tid << "] WRITE to non-stack address: " << std::hex << addr << std::endl;
}

VOID Instruction(INS ins, VOID *v) {
    UINT32 memOps = INS_MemoryOperandCount(ins);

    if (memOps > 0) {  // Check if instruction has explicit memory operands
        for (UINT32 i = 0; i < memOps; i++) {
            if (INS_MemoryOperandIsRead(ins, i)) {
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                               IARG_THREAD_ID,
                               IARG_MEMORYOP_EA, i,  // Get the i-th memory operand address
                               IARG_MEMORYREAD_SIZE,
                               IARG_END);
            }

            if (INS_MemoryOperandIsWritten(ins, i)) {
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                               IARG_THREAD_ID,
                               IARG_MEMORYOP_EA, i,  // Get the i-th memory operand address
                               IARG_MEMORYWRITE_SIZE,
                               IARG_END);
            }
        }

    }

    

    // for (UINT32 i = 0; i < memOps; i++)
    // {
    //     REG baseReg = INS_OperandMemoryBaseReg(ins, i);

    //     // READ
    //     if (INS_MemoryOperandIsRead(ins, i) &&
    //         !(baseReg==REG_STACK_PTR || baseReg == REG_RBP)) {
    //         INS_InsertPredicatedCall(ins, IPOINT_BEFORE,
    //             (AFUNPTR)RecordNonStackRead,
    //             IARG_THREAD_ID,
    //             IARG_MEMORYOP_EA, i,
    //             IARG_END);
    //     }

    //     // WRITE
    //     if (INS_MemoryOperandIsWritten(ins, i) &&
    //         !((baseReg==REG_STACK_PTR || baseReg == REG_RBP))) {
    //         INS_InsertPredicatedCall(ins, IPOINT_BEFORE,
    //             (AFUNPTR)RecordNonStackWrite,
    //             IARG_THREAD_ID,
    //             IARG_MEMORYOP_EA, i,
    //             IARG_END);
    //     }
    // }


}



// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID* v)
{
    

    RTN_Open(rtn);

    // Insert a call at the entry point of a routine to increment the call count
	
	mp[RTN_Address(rtn)]=RTN_Name(rtn);
	
	RTN_InsertCall(rtn,IPOINT_BEFORE, (AFUNPTR)checkmain, IARG_PTR, mp[RTN_Address(rtn)].c_str(), IARG_END);
	
   	RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)beginfunc, IARG_PTR, mp[RTN_Address(rtn)].c_str(), IARG_THREAD_ID, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1 ,IARG_END);

	RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)retfunc, IARG_PTR, mp[RTN_Address(rtn)].c_str(), IARG_THREAD_ID, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_END);

    
    RTN_Close(rtn);
}

// This function is called when the application exits
// It prints the name and count for each procedure
VOID Fini(INT32 code, VOID* v)
{
    outFile<<"The transactions are serialisable"<<endl;
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This Pintool counts the number of times a routine is executed" << endl;
    cerr << "and the number of instructions executed in a routine" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    // Initialize symbol table code, needed for rtn instrumentation
    PIN_InitSymbols();

    outFile.open("serialisable.out");

    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();
    PIN_InitLock(&pinl);
	
	//initialise function
	Initialise();	



    RTN_AddInstrumentFunction(Routine, 0);
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
