import("fork_version", { rootdir = path.join(path.directory(os.scriptdir()), "scripts") })

local function git(repository, arguments)
    local command = { "-C", repository }
    for _, argument in ipairs(arguments) do
        table.insert(command, argument)
    end
    return os.iorunv("git", command)
end

local function failure(callback)
    local message
    try({
        callback,
        catch({
            function(errors)
                message = tostring(errors)
            end,
        }),
    })
    return message
end

local function commit_file(repository, filename, contents, subject)
    io.writefile(path.join(repository, filename), contents)
    git(repository, { "add", filename })
    git(repository, { "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", subject })
end

function main()
    local repository = path.absolute(os.tmpfile() .. "-fork-version")
    assert(path.directory(repository) == path.absolute(os.tmpdir()))
    os.mkdir(repository)

    try({
        function()
            git(repository, { "init", "-q" })
            commit_file(repository, "fixture.txt", "first\n", "first")

            assert(failure(function()
                fork_version(repository)
            end):find("Cannot derive fork version from Git", 1, true))
            git(repository, { "tag", "pt-v1.16.1" })
            local release, release_suffix = fork_version(repository)
            assert(release == "1.16.1" and release_suffix == "")

            commit_file(repository, "fixture.txt", "second\n", "second")
            local development, development_suffix = fork_version(repository)
            assert(development == "1.16.1")
            assert(development_suffix:match("^%.dev%.1%.[0-9a-f]+$"))

            local primary = git(repository, { "branch", "--show-current" }):trim()
            git(repository, { "switch", "-q", "-c", "side", "HEAD~1" })
            commit_file(repository, "side.txt", "side\n", "side")
            git(repository, { "tag", "pt-v9.0.0" })
            git(repository, { "switch", "-q", primary })
            git(repository, {
                "-c",
                "user.name=Fixture",
                "-c",
                "user.email=fixture@example.invalid",
                "merge",
                "--no-ff",
                "-qm",
                "side",
                "side",
            })
            local unqualified = git(repository, { "describe", "--tags", "--long", "--match=pt-v[0-9]*", "HEAD" })
            assert(unqualified:match("^pt%-v9%.0%.0"))
            local merged, merged_suffix = fork_version(repository)
            assert(merged == "1.16.1")
            assert(merged_suffix:match("^%.dev%.3%.[0-9a-f]+$"))

            git(repository, { "tag", "pt-v1.16.2-beta" })
            assert(failure(function()
                fork_version(repository)
            end):find("Expected a reachable pt-vX.Y.Z tag", 1, true))
        end,
        finally({
            function()
                os.rm(repository)
            end,
        }),
    })
end
