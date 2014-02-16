#include "prettifysegfault.h"
#include "backtrace.h"
#include "signalname.h"
#include "expectexception.h"
#include "tasktimer.h"
#include "exceptionassert.h"

#include <signal.h>

bool is_doing_signal_handling = false;
bool has_caught_any_signal_value = false;
bool enable_signal_print = true;
bool nested_signal_handling = false;

void handler(int sig);
void printSignalInfo(int sig);

void seghandle_userspace() {
  // note: because we set up a proper stackframe,
  // unwinding is safe from here.
  // also, by the time this function runs, the
  // operating system is "done" with the signal.

  // choose language-appropriate throw instruction
  // raise new MemoryAccessError "Segmentation Fault";
  // throw new MemoryAccessException;
  // longjmp(erroneous_exit);
  // asm { int 3; }
  // *(int*) NULL = 0;

    if (!nested_signal_handling)
        printSignalInfo(SIGSEGV);
}


void seghandle(int, __siginfo*, void* unused)
{    
  nested_signal_handling = is_doing_signal_handling;
  has_caught_any_signal_value = true;
  is_doing_signal_handling = true;

  fflush(stdout);
  fprintf(stderr, "\nError: signal %s(%d) %s\n", SignalName::name (SIGSEGV), SIGSEGV, SignalName::desc (SIGSEGV));
  fflush(stderr);

  ucontext_t* uc = (ucontext_t*) unused;
  // No. I refuse to use triple-pointers.
  // Just pretend ref a = v; is V* ap = &v;
  // and then substitute a with (*ap).
//  ref gregs = uc->uc_mcontext.gregs;
//  ref eip = (void*) gregs[X86Registers.EIP],
//  ref esp = (void**) gregs[X86Registers.ESP];

  // 32-bit
//  void*& eip = (void*&)uc->uc_mcontext->__ss.__eip;
//  void**& esp = (void**&)uc->uc_mcontext->__ss.__esp;

  // 64-bit
  // warning: dereferencing type-punned pointer will break strict-aliasing rules
  // how should this be handled?
  void*& eip = (void*&)uc->uc_mcontext->__ss.__rip;
  void**& esp = (void**&)uc->uc_mcontext->__ss.__rsp;

  // imitate the effects of "call seghandle_userspace"
  esp --; // decrement stackpointer.
          // remember: stack grows down!
  *esp = eip;

  // set up OS for call via return, like in the attack
  eip = (void*) &seghandle_userspace;

  // this is reached because 'ret' jumps to seghandle_userspace
  is_doing_signal_handling = false;
}


void setup_sigsegv()
{
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset (&sa.sa_mask);
  sa.__sigaction_u.__sa_sigaction = &seghandle;
  bool sigsegv_handler = sigaction(SIGSEGV, &sa, NULL) != -1;
  EXCEPTION_ASSERTX(sigsegv_handler, "failed to setup SIGSEGV handler");
}


void handler(int sig)
{
    nested_signal_handling = is_doing_signal_handling;
    has_caught_any_signal_value = true;
    is_doing_signal_handling = true;

    // The intention is that handler(sig) should cause very few stack and heap
    // allocations. And 'printSignalInfo' should prints prettier info but with
    // the expense of a whole bunch of both stack and heap allocations.

    // http://feepingcreature.github.io/handling.html does not work, see note 4.

    fflush(stdout);
    fprintf(stderr, "\nError: signal %s(%d) %s\n", SignalName::name (sig), sig, SignalName::desc (sig));
    fflush(stderr);

    Backtrace::malloc_free_log ();

    if (!nested_signal_handling)
        printSignalInfo(sig);

    // This will not be reached if an exception is thrown
    is_doing_signal_handling = false;
}


void printSignalInfo(int sig)
{
    // Lots of memory allocations here. Not neat, but more helpful.

    if (enable_signal_print)
        TaskInfo("Got %s(%d) '%s'\n%s",
             SignalName::name (sig), sig, SignalName::desc (sig),
             Backtrace::make_string ().c_str());
    fflush(stdout);

    switch(sig)
    {
#ifndef _MSC_VER
    case SIGCHLD:
        return;

    case SIGWINCH:
        TaskInfo("Got SIGWINCH, could reload OpenGL resources here");
        fflush(stdout);
        return;
#endif
    case SIGABRT:
        TaskInfo("Got SIGABRT");
        fflush(stdout);
        exit(1);

    case SIGSEGV:
        if (enable_signal_print)
            TaskInfo("Throwing segfault_exception");
        fflush(stdout);

        BOOST_THROW_EXCEPTION(segfault_exception()
                              << signal_exception::signal(sig)
                              << signal_exception::signalname(SignalName::name (sig))
                              << signal_exception::signaldesc(SignalName::desc (sig))
                              << Backtrace::make (2));

    default:
        if (enable_signal_print)
            TaskInfo("Throwing signal_exception");
        fflush(stdout);

        BOOST_THROW_EXCEPTION(signal_exception()
                              << signal_exception::signal(sig)
                              << signal_exception::signalname(SignalName::name (sig))
                              << signal_exception::signaldesc(SignalName::desc (sig))
                              << Backtrace::make (2));
    }
}


#ifdef _MSC_VER

const char* ExceptionCodeName(DWORD code)
{
    switch(code)
    {
    case WAIT_IO_COMPLETION: return "WAIT_IO_COMPLETION / STATUS_USER_APC";
    case STILL_ACTIVE: return "STILL_ACTIVE, STATUS_PENDING";
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_SINGLE_STEP: return "EXCEPTION_SINGLE_STEP";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT: return "EXCEPTION_FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION: return "EXCEPTION_INVALID_DISPOSITION";
    case EXCEPTION_GUARD_PAGE: return "EXCEPTION_GUARD_PAGE";
    case EXCEPTION_INVALID_HANDLE: return "EXCEPTION_INVALID_HANDLE";
#ifdef STATUS_POSSIBLE_DEADLOCK
    case EXCEPTION_POSSIBLE_DEADLOCK: return "EXCEPTION_POSSIBLE_DEADLOCK";
#endif
    case CONTROL_C_EXIT: return "CONTROL_C_EXIT";
    default: return "UNKNOWN";
    }
}
const char* ExceptionCodeDescription(DWORD code)
{
    switch(code)
    {
    case EXCEPTION_ACCESS_VIOLATION: return "The thread tried to read from or write to a virtual address for which it does not have the appropriate access.";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.";
    case EXCEPTION_BREAKPOINT: return "A breakpoint was encountered.";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "The thread tried to read or write data that is misaligned on hardware that does not provide alignment. For example, 16-bit values must be aligned on 2-byte boundaries; 32-bit values on 4-byte boundaries, and so on.";
    case EXCEPTION_FLT_DENORMAL_OPERAND: return "One of the operands in a floating-point operation is denormal. A denormal value is one that is too small to represent as a standard floating-point value.";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "The thread tried to divide a floating-point value by a floating-point divisor of zero.";
    case EXCEPTION_FLT_INEXACT_RESULT: return "The result of a floating-point operation cannot be represented exactly as a decimal fraction.";
    case EXCEPTION_FLT_INVALID_OPERATION: return "This exception represents any floating-point exception not included in this list.";
    case EXCEPTION_FLT_OVERFLOW: return "The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.";
    case EXCEPTION_FLT_STACK_CHECK: return "The stack overflowed or underflowed as the result of a floating-point operation.";
    case EXCEPTION_FLT_UNDERFLOW: return "The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "The thread tried to execute an invalid instruction.";
    case EXCEPTION_IN_PAGE_ERROR: return "The thread tried to access a page that was not present, and the system was unable to load the page. For example, this exception might occur if a network connection is lost while running a program over the network.";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "The thread tried to divide an integer value by an integer divisor of zero.";
    case EXCEPTION_INT_OVERFLOW: return "The result of an integer operation caused a carry out of the most significant bit of the result.";
    case EXCEPTION_INVALID_DISPOSITION: return "An exception handler returned an invalid disposition to the exception dispatcher. Programmers using a high-level language such as C should never encounter this exception.";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "The thread tried to continue execution after a noncontinuable exception occurred.";
    case EXCEPTION_PRIV_INSTRUCTION: return "The thread tried to execute an instruction whose operation is not allowed in the current machine mode.";
    case EXCEPTION_SINGLE_STEP: return "A trace trap or other single-instruction mechanism signaled that one instruction has been executed.";
    case EXCEPTION_STACK_OVERFLOW: return "The thread used up its stack.";
    default: return "No description";
    }
}

LONG WINAPI MyUnhandledExceptionFilter(
  _In_  struct _EXCEPTION_POINTERS *ExceptionInfo
)
{
    DWORD code = ExceptionInfo->ExceptionRecord->ExceptionCode;
    TaskInfo ti("Caught UnhandledException %s(%d) %s", ExceptionCodeName(code), code, ExceptionCodeDescription(code));

    // Translate from Windows Structured Exception to C signal.
    //C signals known in Windows:
    // http://msdn.microsoft.com/en-us/library/xdkz3x12(v=vs.110).aspx
    //#define SIGINT          2       /* interrupt */
    //#define SIGILL          4       /* illegal instruction - invalid function image */
    //#define SIGFPE          8       /* floating point exception */
    //#define SIGSEGV         11      /* segment violation */
    //#define SIGTERM         15      /* Software termination signal from kill */
    //#define SIGBREAK        21      /* Ctrl-Break sequence */
    //#define SIGABRT         22      /* abnormal termination triggered by abort call */

    //#define SIGABRT_COMPAT  6       /* SIGABRT compatible with other platforms, same as SIGABRT */

    // http://msdn.microsoft.com/en-us/library/windows/desktop/aa363082(v=vs.85).aspx
    switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_INVALID_DISPOSITION:
    case EXCEPTION_GUARD_PAGE:
    case EXCEPTION_INVALID_HANDLE:
#ifdef STATUS_POSSIBLE_DEADLOCK
    case EXCEPTION_POSSIBLE_DEADLOCK:
#endif
    case CONTROL_C_EXIT:
        handler(SIGSEGV);
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        handler(SIGILL);
        break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
        handler(SIGFPE);
        break;
    case EXCEPTION_BREAKPOINT:
    case EXCEPTION_SINGLE_STEP:
    default:
        break;
    }
    return EXCEPTION_CONTINUE_SEARCH; // default exception handling, handler usually throws a C++ exception before this line.
}
#endif // _MSC_VER


void PrettifySegfault::
        setup ()
{
#ifndef _MSC_VER
    // subscribe to everything SIGSEGV and friends
    for (int i=1; i<=SIGUSR2; ++i)
    {
        switch(i)
        {
        case SIGCHLD:
            break;
        case SIGSEGV:
            setup_sigsegv();
            break;
        default:
            if (0!=strcmp(SignalName::name (i),"UNKNOWN"))
                signal(i, handler); // install our handler
            break;
        }
    }

#else
    SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);
#endif
}


PrettifySegfault::SignalHandlingState PrettifySegfault::
        signal_handling_state()
{
    return is_doing_signal_handling ? PrettifySegfault::doing_signal_handling : PrettifySegfault::normal_execution;
}


void PrettifySegfault::
        EnableDirectPrint(bool enable)
{
    enable_signal_print = enable;
}


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnull-dereference"
#pragma clang diagnostic ignored "-Wself-assign"


void throw_catcheable_segfault_exception()
{
    *(int*) NULL = 0;
    throw std::exception();
}


void throw_catcheable_segfault_exception2()
{
    TaskInfo("throw_catcheable_segfault_exception2");
    *(int*) NULL = 0;
}


void throw_uncatcheable_segfault_exception()
{
    *(int*) NULL = 0;
}


void throw_uncatcheable_segfault_exception2()
{
    int a=0;
    a=a;
    *(int*) NULL = 0;
}
#pragma clang diagnostic pop


#include "detectgdb.h"


void PrettifySegfault::
        test()
{
    // Skip test if running through gdb
    if (DetectGdb::is_running_through_gdb ())
        return;

    // It should attempt to capture any null-pointer exception in the program
    // and throw a controlled C++ exception instead from that location instead.
    {
        enable_signal_print = false;
        EXPECT_EXCEPTION(segfault_exception, throw_catcheable_segfault_exception());
        EXPECT_EXCEPTION(segfault_exception, throw_catcheable_segfault_exception2());
        enable_signal_print = true;
    }
}
