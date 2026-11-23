-- In-tree build of cpp-mcp (https://github.com/hkr04/cpp-mcp), vendored as the
-- lib/cpp-mcp submodule and pinned to commit a0eb22c (the same tree Community
-- Shaders vendors). Mirrors CS's cmake/cpp-mcp.cmake recipe.
--
-- Only the server-side TUs are compiled (mcp_message/resource/server/tool); the
-- stdio/SSE *client* implementations are intentionally omitted — devbench is a
-- server only. Upstream ships no install rules (PR #12 open), so we drive the
-- build ourselves rather than consume it as a package.
--
-- Source edits are applied into a build-tree mirror so the submodule
-- stays clean:
--   1. mcp_message.h: `#include "json.hpp"` -> `#include <nlohmann/json.hpp>`,
--      so cpp-mcp and devbench share one nlohmann_json ABI (the vendored 3.11.3
--      and our 3.12.0 wrap their API in different ABI-tagged inline namespaces;
--      mixing them is an LNK2001 at link time).
--   2. mcp_server.h: insert a public `http()` getter returning the underlying
--      httplib::Server*, so the REST facade can mount routes on the MCP port
--      (the cpp-mcp-expose-http.patch, applied here as a string edit).
--   3. Replace the lossy SSE event slot with a bounded FIFO. Announce tools on
--      initialized sessions and expire sessions whose event queues overflow.

local cpp_mcp_root = path.join(os.projectdir(), "lib", "cpp-mcp")
local patched_inc = path.join(os.projectdir(), "build", "cpp-mcp-patched", "include")
local patched_src = path.join(os.projectdir(), "build", "cpp-mcp-patched", "src", "mcp_server.cpp")

target("cpp-mcp")
set_kind("static")
set_languages("c++17")
set_group("extern")

add_files(
    path.join(cpp_mcp_root, "src", "mcp_message.cpp"),
    path.join(cpp_mcp_root, "src", "mcp_resource.cpp"),
    patched_src,
    path.join(cpp_mcp_root, "src", "mcp_tool.cpp")
)

-- Patched mirror first so its mcp_message.h / mcp_server.h win over the
-- submodule's; common/ supplies httplib.h (no shared-ABI concern there).
add_includedirs(patched_inc, path.join(cpp_mcp_root, "common"), { public = true })

add_defines(
    "MCP_MAX_SESSIONS=10",
    "MCP_SESSION_TIMEOUT=30",
    "_WINSOCKAPI_",
    "_CRT_SECURE_NO_WARNINGS",
    { public = true }
)

add_cxflags("/utf-8", "/bigobj", "/W0", "/std:c++17", { force = true })
add_packages("nlohmann_json")
add_syslinks("ws2_32", "crypt32")

-- Generate the patched header mirror before compiling. on_load runs with the
-- full os/io/raise API available (unlike the description sandbox).
on_load(function(target)
    local root = path.join(os.projectdir(), "lib", "cpp-mcp")
    local out = path.join(os.projectdir(), "build", "cpp-mcp-patched", "include")
    if not os.isfile(path.join(root, "src", "mcp_server.cpp")) then
        raise("cpp-mcp submodule missing. Run: git submodule update --init --recursive lib/cpp-mcp")
    end
    os.mkdir(out)
    for _, hdr in ipairs(os.files(path.join(root, "include", "*.h"))) do
        local name = path.filename(hdr)
        local content = io.readfile(hdr)
        if name == "mcp_message.h" then
            if not content:find('#include "json.hpp"', 1, true) then
                raise(
                    'cpp-mcp: expected `#include "json.hpp"` in mcp_message.h; upstream changed — review xmake/cpp-mcp.lua'
                )
            end
            content = content:gsub('#include "json%.hpp"', "#include <nlohmann/json.hpp>")
        elseif name == "mcp_server.h" then
            local anchor = "\nprivate:\n    std::string host_;"
            if not content:find(anchor, 1, true) then
                raise(
                    "cpp-mcp: expected private section anchor in mcp_server.h; upstream changed — review xmake/cpp-mcp.lua"
                )
            end
            local getter = "\n"
                .. "    /**\n"
                .. "     * @brief Access the underlying httplib server to register custom routes.\n"
                .. "     * Lets a consumer mount additional endpoints (e.g. a REST facade) on the\n"
                .. "     * same host/port as the MCP transport. Register routes before start().\n"
                .. "     */\n"
                .. "    httplib::Server* http() { return http_server_.get(); }\n"
                .. anchor
            content = content:gsub("\nprivate:\n    std::string host_;", getter, 1)
            local first = assert(content:find("class event_dispatcher {", 1, true))
            local last = assert(content:find("\n};", first, true)) + 3
            content = content:sub(1, first - 1)
                .. "using event_dispatcher = dvb::McpEventDispatcher;"
                .. content:sub(last + 1)
            content = content:gsub('#include "httplib.h"', '#include "httplib.h"\n#include "McpEventDispatcher.h"', 1)
        end
        io.writefile(path.join(out, name), content)
    end
    io.writefile(
        path.join(out, "McpEventDispatcher.h"),
        io.readfile(path.join(os.projectdir(), "src", "McpEventDispatcher.h"))
    )
    local source = io.readfile(path.join(root, "src", "mcp_server.cpp"))
    local function replace_once(before, after)
        local first, last = source:find(before, 1, true)
        if not first or source:find(before, last + 1, true) then
            raise("cpp-mcp: server patch anchor changed; review xmake/cpp-mcp.lua")
        end
        source = source:sub(1, first - 1) .. after .. source:sub(last + 1)
    end
    replace_once(
        "            set_session_initialized(session_id, true);",
        [[            const bool firstInitialization = !is_session_initialized(session_id);
            set_session_initialized(session_id, true);
            if (firstInitialization && capabilities_.contains("tools") && capabilities_["tools"].value("listChanged", false)) {
                send_request(session_id, request::create_notification("tools/list_changed", json::object()));
            }]]
    )
    replace_once(
        '        LOG_WARNING("Cannot send to closed session: ", session_id);',
        '        LOG_WARNING("Cannot send to closed session: ", session_id);\n        close_session(session_id);'
    )
    replace_once(
        '        LOG_ERROR("Failed to send message to session: ", session_id);',
        '        LOG_ERROR("Failed to send message to session: ", session_id);\n        close_session(session_id);'
    )
    os.mkdir(path.directory(patched_src))
    io.writefile(patched_src, source)
end)
target_end()
