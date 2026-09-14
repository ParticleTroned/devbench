-- The release tag is the fork version; untagged commits carry their Git identity.
function parse(description)
    local version, distance, commit = description:match("^pt%-v(%d+%.%d+%.%d+)%-(%d+)%-g([0-9a-f]+)$")
    if not version then
        raise("Expected a reachable pt-vX.Y.Z tag; fetch tags before building")
    end
    if tonumber(distance) == 0 then
        return version, ""
    end
    return version, ".dev." .. distance .. "." .. commit
end

function main(repository)
    local description
    try({
        function()
            description = os.iorunv("git", {
                "-C",
                repository,
                "describe",
                "--tags",
                "--long",
                "--match=pt-v[0-9]*",
                "HEAD",
            })
        end,
        catch({
            function()
                raise("Cannot derive fork version from Git; fetch reachable pt-vX.Y.Z tags before building")
            end,
        }),
    })
    return parse(description:trim())
end
