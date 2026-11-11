# Publishing (nothing here has been run)

Everything below is for you to run later. Nothing was pushed, forked or opened.
Commits are local on `screencast-capture`.

## 0. Look before you push

```bash
cd "/home/harsh/Documents/GITHUB PROJECTS/flameshot-fast"
git status
git log --oneline v15.0.0..screencast-capture
git remote -v        # origin = your fork, upstream = flameshot-org/flameshot
```

## 1. Push your fork (origin already points at harshp2008/flameshot-fast)

The fork must exist on GitHub first (github.com/flameshot-org/flameshot ->
Fork, name it `flameshot-fast`), or with the CLI:

```bash
gh repo fork flameshot-org/flameshot --clone=false --fork-name flameshot-fast
```

Then:

```bash
git push -u origin screencast-capture
git push origin --tags         # only if you want the upstream tags on the fork
```

`master` stays a mirror: `git fetch upstream && git checkout master && git merge --ff-only upstream/master && git push origin master`.

## 2. Ask upstream before sending code (their process)

Flameshot wants an RFC for substantial changes (docs/RFC.md) and prefers
talking first (Matrix space or GitHub Discussions). Suggested order:

1. Open a Discussion with the text of `fast/docs/UPSTREAM_RFC_DRAFT.md`
   (short form), numbers included.
2. If maintainers are open to an optional backend, send the RFC as a PR that
   adds `docs/RFC/0000-screencast-capture.md` (copy the draft).

```bash
gh api -X GET repos/flameshot-org/flameshot/discussions 2>/dev/null | head   # not required
gh discussion create --repo flameshot-org/flameshot \
   --title "Optional ScreenCast-portal capture backend (per-monitor, ~0.15 s on multi-monitor GNOME)" \
   --body-file fast/docs/UPSTREAM_RFC_DRAFT.md
```

(`gh discussion` may need the `gh-discussion` extension; otherwise paste the
text in the web UI under Discussions -> New -> Ideas.)

## 3. Only if an RFC is accepted: a code PR

Upstream wants one change per branch, clean history, formatted with
clang-format. The code commits are separable from the `fast/` files:

```bash
git checkout -b screencast-upstream v15.0.0     # or the current upstream/master
git cherry-pick 1dd28d9b 621b9cf8 fd430793 6500f73b   # code only (verify hashes with git log)
clang-format-18 -i $(git diff --name-only HEAD~4 | grep -E '\.(cpp|h)$')
git push -u origin screencast-upstream
gh pr create --repo flameshot-org/flameshot --base master \
   --head harshp2008:screencast-upstream \
   --title "Add optional ScreenCast portal capture backend" \
   --body-file fast/docs/UPSTREAM_RFC_DRAFT.md
```

Open it ready for review (no `--draft`). Do not add a Co-Authored-By trailer
or a generated-by line. Use `git log` to check the commit hashes above; they
change if you rebase.

## 4. Releases of your fork (optional)

```bash
git tag -a fast-v15.0.0-1 -m "flameshot-fast 15.0.0 + ScreenCast backend"
git push origin fast-v15.0.0-1
```
