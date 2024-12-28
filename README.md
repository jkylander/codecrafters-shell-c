[![progress-banner](https://backend.codecrafters.io/progress/shell/d4830689-26e8-4149-9c98-6808175072a6)](https://app.codecrafters.io/users/codecrafters-bot?r=2qF)

This is a starting point for C solutions to the
["Build Your Own Shell" Challenge](https://app.codecrafters.io/courses/shell/overview).

In this challenge, you'll build your own POSIX compliant shell that's capable of
interpreting shell commands, running external programs and builtin commands like
cd, pwd, echo and more. Along the way, you'll learn about shell command parsing,
REPLs, builtin commands, and more.

**Note**: If you're viewing this repo on GitHub, head over to
[codecrafters.io](https://codecrafters.io) to try the challenge.

1. ensure you have `c` installed locally
2. run `./your_program.sh` to run your program, which is implemented in
   `app/main.c`.


```mermaid
graph TB
    subgraph "User Interface Layer"
        UI[User Input]
        REPL[REPL/Input Handler]:::core
    end

    subgraph "Command Processing Layer"
        Parser[Command Parser/Lexer]:::core
        Validator[Syntax Validator]:::core
    end

    subgraph "Command Execution Layer"
        CmdExec[Command Executor]:::core
        decision{Built-in?}
        BuiltIn[Built-in Commands Handler]:::core
        ExtExec[External Program Executor]:::core
    end

    subgraph "System Interface Layer"
        ProcMgmt[Process Management]:::system
        FileSystem[File System Interface]:::system
        EnvVars[Environment Variables]:::system
    end

    %% Relationships
    UI --> REPL
    REPL --> Parser
    Parser --> Validator
    Validator --> CmdExec
    CmdExec --> decision
    decision -->|Yes| BuiltIn
    decision -->|No| ExtExec
    
    BuiltIn -.->|Interacts| FileSystem
    BuiltIn -.->|Manages| EnvVars
    ExtExec -->|Uses| ProcMgmt
    ProcMgmt -.->|fork/exec| SystemCalls[System Calls]:::system
    
    %% System Call Connections
    ExtExec -.->|execve| SystemCalls
    ProcMgmt -.->|wait| SystemCalls
    FileSystem -.->|chdir/getcwd| SystemCalls

    %% Click Events
    click REPL "https://github.com/jkylander/codecrafters-shell-c/blob/master/app/main.c"
    click Parser "https://github.com/jkylander/codecrafters-shell-c/blob/master/app/main.c"
    click CmdExec "https://github.com/jkylander/codecrafters-shell-c/blob/master/app/main.c"
    click ExtExec "https://github.com/jkylander/codecrafters-shell-c/blob/master/.codecrafters/run.sh"
    click BuiltIn "https://github.com/jkylander/codecrafters-shell-c/blob/master/app/main.c"

    %% Styles
    classDef core fill:#2374ab,stroke:#2374ab,color:white
    classDef system fill:#57a773,stroke:#57a773,color:white
    classDef external fill:#ffc857,stroke:#ffc857,color:black

    %% Legend
    subgraph Legend
        L1[Core Components]:::core
        L2[System Components]:::system
        L3[External Components]:::external
    end
```

