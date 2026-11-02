#ifndef COMMANDSUGGEST_AS_H
#define COMMANDSUGGEST_AS_H

static char __cmd__sugg__source[] = R"LUA(
local command = string.match(cmdline or "", "^%s*(%S+)")
if not command then
    return
end

command = string.lower(command)
if #command < 2 then
    return
end

-- Keep this list in sync with the shell command table.
local commands = {
    "help", "whoami", "ls", "cd", "pwd", "touch", "setattr", "mkdir",
    "rm", "nukedir", "cp", "mv", "cat", "run", "rl", "su",
    "lsblk", "mount", "umount", "dd", "format", "moudump", "clear",
    "mem", "sinfo", "about", "callt", "reboot", "shutdown"
}

local function edit_distance(a, b)
    local previous_previous
    local previous = {}
    for j = 0, #b do
        previous[j] = j
    end

    for i = 1, #a do
        local current = {[0] = i}
        for j = 1, #b do
            local substitution_cost = 1
            if string.sub(a, i, i) == string.sub(b, j, j) then
                substitution_cost = 0
            end

            local distance = math.min(
                previous[j] + 1,
                current[j - 1] + 1,
                previous[j - 1] + substitution_cost
            )

            if i > 1 and j > 1
                and string.sub(a, i, i) == string.sub(b, j - 1, j - 1)
                and string.sub(a, i - 1, i - 1) == string.sub(b, j, j) then
                distance = math.min(distance, previous_previous[j - 2] + 1)
            end
            current[j] = distance
        end

        previous_previous = previous
        previous = current
    end

    return previous[#b]
end

local function common_prefix_length(a, b)
    local i = 1
    while i <= #a and i <= #b
        and string.sub(a, i, i) == string.sub(b, i, i) do
        i = i + 1
    end
    return i - 1
end

local best_command
local best_distance
local best_prefix
local ambiguous = false

for _, candidate in ipairs(commands) do
    local distance = edit_distance(command, candidate)
    local limit = math.floor(math.max(#command, #candidate) / 4)
    if limit < 1 then
        limit = 1
    elseif limit > 2 then
        limit = 2
    end

    if distance > 0 and distance <= limit then
        local prefix = common_prefix_length(command, candidate)
        if not best_distance or distance < best_distance
            or (distance == best_distance and prefix > best_prefix) then
            best_command = candidate
            best_distance = distance
            best_prefix = prefix
            ambiguous = false
        elseif distance == best_distance and prefix == best_prefix then
            ambiguous = true
        end
    end
end

if best_command and not ambiguous then
    retsuggest(best_command)
end
)LUA";

#endif // COMMANDSUGGEST_AS_H