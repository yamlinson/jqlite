# jqlite

A minimal JSON parser and printer.

## About

I just wrote this as a toy project to practice memory management in C after working through ~ch5 of the K&R.
It doesn't quite meet JSON spec, since it doesn't support unicode `\u` escapes, but it should be pretty close.
It also doesn't offer any real functionality other than formatted printing,
so no filtering or transforming or anything like that (at least currently).

It at least doesn't leak memory, so I'm pretty happy with that.
Valgrind was being difficult on my distro, but AddressSanitizer confirmed.

## Build and test

Compile with `gcc -std=c11 -Wall -Wextra -g -O0 -fsanitize=address,undefined src/jqlite.c -o bin/jqlite`
and run `./tests.sh`.

Some other fun JSON one-liners to run through:

```bash
# GitHub repo stats for a popular project
curl -s https://api.github.com/repos/torvalds/linux | ./bin/jqlite
# Some local landmarks formatted for GeoJSON
printf '%s' '{"type":"FeatureCollection","features":[{"type":"Feature","geometry":{"type":"Point","coordinates":[-111.8882,40.7775]},"properties":{"name":"Utah State Capitol","category":"landmark","completed":1916}},{"type":"Feature","geometry":{"type":"Point","coordinates":[-111.6458,40.3908]},"properties":{"name":"Mount Timpanogos","category":"summit","elevation_m":3582,"range":"Wasatch"}},{"type":"Feature","geometry":{"type":"Point","coordinates":[-111.7713,40.6563]},"properties":{"name":"Mount Olympus","category":"summit","elevation_m":2751,"range":"Wasatch"}},{"type":"Feature","geometry":{"type":"Point","coordinates":[-111.6386,40.5884]},"properties":{"name":"Alta","category":"ski area","canyon":"Little Cottonwood","snowboarding":false}},{"type":"Feature","geometry":{"type":"LineString","coordinates":[[-111.897,40.36],[-111.921,40.5],[-111.93,40.62],[-111.925,40.77],[-111.94,40.88]]},"properties":{"name":"Jordan River","category":"river","flows":"north","from":"Utah Lake","to":"Great Salt Lake"}},{"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[-112.3527734,40.6537717],[-112.1568015,40.8044637],[-112.2951207,41.1851953],[-112.5861967,41.4168568],[-112.85,41.7],[-112.960371,41.4405],[-112.7936964,41.0375437],[-112.6248718,40.7375334],[-112.3527734,40.6537717]]]},"properties":{"name":"Great Salt Lake","category":"lake","water":"hypersaline","outlets":[],"note":"very simplified outline"}}]}' | ./bin/jqlite
```
