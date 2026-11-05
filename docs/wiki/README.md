# Wiki sources / Sorgenti della wiki

These files are the pages of the [project wiki](https://github.com/emanuelemerlo/elevator-upgrade/wiki), kept here so
that they are versioned with the code. The links between pages use the wiki format: read them on the wiki.

Questi file sono le pagine della [wiki del progetto](https://github.com/emanuelemerlo/elevator-upgrade/wiki), tenute
qui per essere versionate con il codice. I collegamenti tra le pagine usano il formato della wiki: leggile sulla wiki.

To publish them / Per pubblicarle:

```sh
git clone https://github.com/emanuelemerlo/elevator-upgrade.wiki.git
cp elevator-upgrade/docs/wiki/*.md elevator-upgrade.wiki/     # Windows: copy elevator-upgrade\docs\wiki\*.md elevator-upgrade.wiki\
rm elevator-upgrade.wiki/README.md                            # Windows: del elevator-upgrade.wiki\README.md
cd elevator-upgrade.wiki && git add -A && git commit -m "Update wiki" && git push
```
