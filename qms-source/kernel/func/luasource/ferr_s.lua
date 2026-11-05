k_req("shell")
k_req("easyinput")
k_req("vfs")
k_req("advancedio")

function __corr_header_(name)
    print("Corrupted header@"..name)
end

function print_prompt()
    advancedio.printgc('@FERR-S\x01', advancedio.colors.bright_blue(), 0)
    advancedio.printgc('[\x01', advancedio.colors.light_gray(), 0)
    advancedio.printgc(vfs.getcwd().."\x01", advancedio.colors.bright_green(), 0)
    advancedio.printgc(']\x01', advancedio.colors.light_gray(), 0)
    advancedio.printgc('v.0.4.\x01', advancedio.colors.light_gray(), 0)
    advancedio.printgc('lua\x01', advancedio.colors.yellow(), 0)
    advancedio.printgc('# \x01', advancedio.colors.light_gray(), 0)
end

local funcs = {}

-- 1. ЗАРАНЕЕ объявляем таблицу cmds как локальную переменную
local cmds = {} 

function funcs.getcwd()
    print(vfs.getcwd())
    return 0
end

function funcs.exit()
    print("Exiting shell...")
    error("Shell closed")
end

function funcs.unknown()
    print("Unknown command. Type 'help' for a list of commands.")
    return 0
end

function funcs.clear()
    clear() -- Вызов системного clear
    return 0
end

-- 2. Теперь help видит локальную cmds (она объявлена выше!)
function funcs.help()
    print("Available commands:")
    for cmd, _ in pairs(cmds) do
        print(" - " .. cmd)
    end
    return 0
end

function funcs.calc()
    print("V.0.3")
    print("Enter expression:")
    local expr = easyinput.input()
    local func, err = load("return " .. expr)
    if not func then
        print("Error: " .. err)
        return 1
    else
        setfenv(func, {})
        local success, result = pcall(func)
        if not success then
            print("Error: " .. result)
            return 1
        else
            if result ~= nil then
                print("Result: " .. tostring(result))
            else
                print("Result is nil")
            end
            return 0
        end
    end
end

-- 3. Наполняем уже существующую таблицу cmds ссылками на функции
cmds["getcwd"] = funcs.getcwd
cmds["help"]   = funcs.help
cmds["exit"]   = funcs.exit
cmds["clear"]  = funcs.clear
cmds["calc"]   = funcs.calc


function run(cmd)
    local func = cmds[cmd]
    if func then
        return func()
    end
    funcs.unknown()
    return 0
end

while true do
    print_prompt()
    local cmd = easyinput.input()

    local result = run(cmd)
    
    if result and result ~= 0 then
        log_error("Error executing command: " .. result)
    end
end
