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

long long thrd = 3; //defined through flags
long long locks; //defined through 
long long vars;

map<ADDRINT,long long> lck, var; //maps number to lock addresses

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

VOID checkmain(const char* str){
	if(strlen(str)>=4 && (str[0]=='m' && str[1]=='a' && str[2]=='i' && str[3]=='n')){
		start=true;
	}

   

    if(strcmp(str, "global_vars")==0){
        glob=true;
    }

	return;
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

    outFile<<"\nAll variables initialised lck-"<<locks<<" "<<vars<<endl<<endl;
  
}


VOID printfunc0(const char* func, THREADID tid){
	if(start) outFile<< func<<" called from "<<tid<<endl;
	return;
}
VOID printfunc1(const char* func, THREADID tid, ADDRINT arg0){
	if(start) outFile<< func<<" called from "<<tid<<" with args "<<hex<<arg0<<endl;
	return;
}

VOID printfunc2(char* func, THREADID tid, ADDRINT arg0, ADDRINT arg1){
	if(start) outFile<< func<<" called from "<<tid<<" with args "<<hex<<arg0<<" and " <<arg1<<dec<<endl;
    if(start){
        if(glob){
            if(strcmp(func,"write")==0){
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
                fork(0,tid);
            }

            if(strncmp(func, "txn", 3)==0){
                begin(tid);
            }

            if(strcmp(func, "pthread_mutex_lock")==0){
                //lock acquire
                if(lock_addr.count(arg0)==1){
                    //lock variable is included
                    acquire(tid, lck[arg0]);
                }
            }

            if(strcmp(func, "pthread_mutex_unlock")==0){
                //lock acquire
                if(lock_addr.count(arg0)==1){
                    //lock variable is included
                    release(tid, lck[arg0]);
                }
            }


        }
    }

    
	return;
}
VOID retfunc(const char* func, THREADID tid){
	if(start) outFile<<func<<" return from "<<tid<<endl;
    // if(strlen(func)>=4 && (func[0]=='m' && func[1]=='a' && func[2]=='i' && func[3]=='n')){
	// 	start=false;
	// }
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

    if(start){
        if(strncmp(func,"thrd",4)==0){
            join(0,tid);
        }

        if(strncmp(func, "txn", 3)==0){
            end(tid);
        }
    }
	return;
}

VOID Initialise(){
	outFile<<"This is initialisation"<<endl;
}

VOID RecordMemRead(THREADID tid, ADDRINT addr, UINT32 size) {
    // if(glob==true){
    // outFile << "[Thread " << tid << "] READ from: " 
    //         << std::hex << addr << " | Size: " << size << " bytes" <<glob<< std::endl;
    // }

    if(start && !glob){
        if(var_addr.count(addr)){
            //address is a global var
            read(tid, var[addr]);
        }
    }

}

VOID RecordMemWrite(THREADID tid, ADDRINT addr, UINT32 size) {
    // if(glob==true){
    // outFile << "[Thread " << tid << "] WRITE to: " 
    //         << std::hex << addr << " | Size: " << size << " bytes" <<glob<< std::endl;
    // }

    if(start && !glob){
        if(var_addr.count(addr)){
            //address is a global var
            write(tid, var[addr]);
        }
    }
    
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
}


// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID* v)
{
    // Allocate a counter for this routine
    RTN_COUNT* rc = new RTN_COUNT;

    // The RTN goes away when the image is unloaded, so save it now
    // because we need it in the fini
    rc->_name     = RTN_Name(rtn);
    rc->_image    = StripPath(IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str());
    rc->_address  = RTN_Address(rtn);
    rc->_icount   = 0;
    rc->_rtnCount = 0;

    // Add to list of routines
    rc->_next = RtnList;
    RtnList   = rc;

    RTN_Open(rtn);

    // Insert a call at the entry point of a routine to increment the call count
	
	mp[RTN_Address(rtn)]=RTN_Name(rtn);
	
	RTN_InsertCall(rtn,IPOINT_BEFORE, (AFUNPTR)checkmain, IARG_PTR, mp[RTN_Address(rtn)].c_str(), IARG_END);
	
    /*if(RTN_NumArgs(rtn)==1){
	
	RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)printfunc1, IARG_PTR, mp[RTN_Address(rtn)].c_str(), IARG_THREAD_ID, IARG_FUNCARG_ENTRYPOINT_VALUE, 0 ,IARG_END);
}else if(RTN_NumArgs(rtn)==2){
	*/RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)printfunc2, IARG_PTR, mp[RTN_Address(rtn)].c_str(), IARG_THREAD_ID, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1 ,IARG_END);
/*}else{

	RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)printfunc0, IARG_PTR, mp[RTN_Address(rtn)].c_str(), IARG_THREAD_ID ,IARG_END);
}*/

	RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_rtnCount), IARG_END);

    // For each instruction of the routine
    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
    {
        // Insert a call to docount to increment the instruction counter for this rtn
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_icount), IARG_END);
    }

	RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)retfunc, IARG_PTR, mp[RTN_Address(rtn)].c_str(), IARG_THREAD_ID, IARG_END);
    RTN_Close(rtn);
}

// This function is called when the application exits
// It prints the name and count for each procedure
VOID Fini(INT32 code, VOID* v)
{
   /* outFile << setw(23) << "Procedure"
            << " " << setw(15) << "Image"
            << " " << setw(18) << "Address"
            << " " << setw(12) << "Calls"
            << " " << setw(12) << "Instructions" << endl;

    for (RTN_COUNT* rc = RtnList; rc; rc = rc->_next)
    {
        if (rc->_icount > 0 && rc->_name.size()>=3 && rc->_name[0]=='t' && rc->_name[1]=='x' && rc->_name[2]=='n')
            outFile << setw(23) << rc->_name << " " << setw(15) << rc->_image << " " << setw(18) << hex << rc->_address << dec
                    << " " << setw(12) << rc->_rtnCount << " " << setw(12) << rc->_icount << endl;
    }*/
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

    outFile.open("proccount.out");

    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();
	
	//initialise data
	Initialise();	


    // Register Routine to be called to instrument rtn
    RTN_AddInstrumentFunction(Routine, 0);
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
