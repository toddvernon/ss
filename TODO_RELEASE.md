# ss Release TODO

## Screenshots
- [x] Take terminal screenshots (Hero.png, Formula.png, Help.png in docs/)

## Check site locally
- [x] `open docs/index.html` in browser, verify screenshots load and layout looks good

## Make repo public
- [x] GitHub > toddvernon/ss > Settings > Danger Zone > Change visibility > Public

## Enable GitHub Pages
- [x] `gh api repos/toddvernon/ss/pages -X POST -f source.branch=main -f source.path=/docs`
- [ ] Verify https://toddvernon.github.io/ss/ serves

## Update portfolio site
- [ ] Add ss card to `toddvernon.github.io/index.html` (3-column grid, green #10b981 accent)
- [ ] Push and verify https://toddvernon.github.io/

## First release
- [ ] `./release.sh 1.0`
- [ ] Verify download buttons resolve on the site
