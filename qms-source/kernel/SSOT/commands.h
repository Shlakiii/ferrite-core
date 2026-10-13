#ifndef COMMANDS_H
#define COMMANDS_H

namespace Commands {
    inline const char* help() { return "help"; }
    inline const char* list() { return "list"; }
    inline const char* touch() { return "touch "; }
    inline const char* write() { return "wrd "; }
    inline const char* read() { return "cat "; }
    inline const char* remove() { return "rm "; }
    inline const char* clear() { return "clear"; }
    inline const char* mem() { return "mem"; }
    inline const char* calc() { return "calc"; }
    inline const char* edit() { return "edit"; }
    // синонимы

    inline const char* ls() { return "ls"; }
    inline const char* clr() { return "clr"; }
    inline const char* memory() { return "memory"; }
}

#endif