#include <napi.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <fcntl.h>
#include <string>

using namespace Napi;

Value ExecuteCode(const CallbackInfo& info) {
    Env env = info.Env();

    if (info.Length() < 5) {
        TypeError::New(env, "Expected 5 arguments: executable_path, time_limit, memory_limit, output_path, input_path")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string exe_path = info[0].As<String>().Utf8Value();
    int time_limit_ms = info[1].As<Number>().Int32Value();
    int memory_limit_mb = info[2].As<Number>().Int32Value();
    std::string output_path = info[3].As<String>().Utf8Value();
    std::string input_path = info[4].As<String>().Utf8Value();

    pid_t pid = fork();

    if (pid < 0) {
        return Number::New(env, -1);
    } 
    
    if (pid == 0) {
        // Child process
        struct rlimit rl;
        
        // Memory limit
        if (memory_limit_mb > 0) {
            rl.rlim_cur = memory_limit_mb * 1024 * 1024;
            rl.rlim_max = memory_limit_mb * 1024 * 1024;
            setrlimit(RLIMIT_AS, &rl);
        }
        
        // Input redirection
        if (!input_path.empty()) {
            int in_fd = open(input_path.c_str(), O_RDONLY);
            if (in_fd >= 0) {
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }
        }

        // Output redirection
        int out_fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd >= 0) {
            dup2(out_fd, STDOUT_FILENO);
            dup2(out_fd, STDERR_FILENO);
            close(out_fd);
        }

        char *args[] = { (char*)exe_path.c_str(), NULL };
        execvp(exe_path.c_str(), args);
        exit(1);
    } else {
        // Parent process
        int status;
        struct rusage usage;
        
        struct timeval start_time, current_time;
        gettimeofday(&start_time, NULL);
        
        int wpid = 0;
        int elapsed_ms = 0;
        
        // Time limit watchdog
        while (elapsed_ms <= time_limit_ms) {
            wpid = wait4(pid, &status, WNOHANG, &usage);
            if (wpid != 0) {
                break; // Child finished
            }
            usleep(1000); // Sleep 1ms
            
            gettimeofday(&current_time, NULL);
            elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 + 
                         (current_time.tv_usec - start_time.tv_usec) / 1000;
        }
        
        Object result = Object::New(env);
        
        if (wpid == 0) {
            // Child still running, kill it due to TLE
            kill(pid, SIGKILL);
            wait4(pid, &status, 0, &usage); // Reaping
            result.Set("status", Number::New(env, 2));
            result.Set("time", Number::New(env, time_limit_ms));
            result.Set("memory", Number::New(env, usage.ru_maxrss / 1048576.0)); // ru_maxrss is in bytes on macOS
            return result;
        }

        long time_taken = (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000 +
                          (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000;
        
        double memory_mb = usage.ru_maxrss / 1048576.0; // Bytes to MB

        result.Set("time", Number::New(env, time_taken));
        result.Set("memory", Number::New(env, memory_mb));

        if (WIFSIGNALED(status)) {
             int sig = WTERMSIG(status);
             if (sig == SIGSEGV || sig == SIGABRT) {
                 result.Set("status", Number::New(env, 3));
                 return result; // 3: MLE or RE
             }
             if (sig == SIGKILL) {
                result.Set("status", Number::New(env, 2));
                return result; // 2: TLE
             }
             result.Set("status", Number::New(env, 4));
             return result; // 4: RE
        }

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) != 0) {
                result.Set("status", Number::New(env, 4));
                return result; // 4: RE
            }
            result.Set("status", Number::New(env, 1));
            return result; // 1: OK
        }

        result.Set("status", Number::New(env, 0));
        return result; 
    }
}

Object Init(Env env, Object exports) {
    exports.Set(String::New(env, "executeCode"), Function::New(env, ExecuteCode));
    return exports;
}

NODE_API_MODULE(executor, Init)