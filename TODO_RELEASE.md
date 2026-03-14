# ss Release TODO

## Screenshots
- [ ] Take terminal screenshot of main grid, save as `docs/SpreadsheetView.png`
- [ ] Take terminal screenshot of formula editing or cell hunt, save as `docs/FormulaMode.png`

## Check site locally
- [ ] `open docs/index.html` in browser, verify screenshots load and layout looks good

## Make repo public
- [ ] GitHub > toddvernon/ss > Settings > Danger Zone > Change visibility > Public

## Enable GitHub Pages
- [ ] `gh api repos/toddvernon/ss/pages -X POST -f source.branch=main -f source.path=/docs`
- [ ] Verify https://toddvernon.github.io/ss/ serves

## Update portfolio site
- [ ] Add ss card to `toddvernon.github.io/index.html` (3-column grid, green #10b981 accent)
- [ ] Push and verify https://toddvernon.github.io/

## First release
- [ ] `./release.sh 1.0`
- [ ] Verify download buttons resolve on the site
