# Chronicle — Skyrim SE/AE chat and activity log
# Copyright (C) 2026 kezc7
# This file is part of Chronicle and is licensed under the GNU GPL, version 3
# or any later version. See LICENSE.

# A clean never deletes a git worktree. Before a script empties a folder, it
# asks git for every worktree of the repository and stops, naming each one,
# if any lies inside that folder (or is the folder). Anything uncommitted in a
# worktree would be gone for good. Dot-source this file, then call
# Assert-NoWorktreeInside right before the Remove-Item.

# The worktrees in `git worktree list --porcelain` output that lie inside
# $Folder, as git names them. Paths compare whole folder names, ignoring case
# and the kind of slash.
function Get-WorktreesInside {
    param(
        [Parameter(Mandatory)][string]$Folder,
        [Parameter(Mandatory)][AllowEmptyCollection()][AllowEmptyString()][string[]]$Porcelain
    )
    $root = [System.IO.Path]::GetFullPath($Folder).TrimEnd('\', '/') + '\'
    foreach ($line in $Porcelain) {
        if ($line -notmatch '^worktree (.+)$') { continue }
        $named = $Matches[1]
        $tree = [System.IO.Path]::GetFullPath($named).TrimEnd('\', '/') + '\'
        if ($tree.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) { $named }
    }
}

# Stops unless git can list the worktrees of $Repo and none lies inside $Folder.
function Assert-NoWorktreeInside {
    param(
        [Parameter(Mandatory)][string]$Folder,
        [Parameter(Mandatory)][string]$Repo
    )
    if (!(Get-Command git -ErrorAction SilentlyContinue)) {
        throw "[Chronicle] git isn't on PATH, so the worktrees can't be checked; $Folder not emptied"
    }
    $porcelain = @(& git -C $Repo worktree list --porcelain)
    if ($LASTEXITCODE -ne 0) { throw "[Chronicle] git couldn't list the worktrees of $Repo; $Folder not emptied" }
    $inside = @(Get-WorktreesInside -Folder $Folder -Porcelain $porcelain)
    if ($inside.Count -gt 0) {
        throw ("[Chronicle] $Folder holds a git worktree (" + ($inside -join ', ') + "); not emptied. " +
            'Commit its work and move or remove that worktree first. Never run git worktree prune for this.')
    }
}
