# zpool summary

Write a short one line summary of all (almost, see below) ZFS storage pools to `stdout`.

For status bars (like [Polybar](https://github.com/polybar/polybar)).

 * A pool is considered low on space if less than 5% is available for 1 TB and up, or less than 10% for smaller pools.
 * Tiny pools (< 5GB) without errors that aren't low are not included in the output (e.g. `bootpool`).

_This also serves as a sample application for the [snn-core][snncore] library and can be built with the [build-tool][buildtool]._


## Usage

```console
$ zpool-summary
zroot: 332G
```

```console
$ zpool-summary
zroot: 332G tank: 372G
```

```console
$ zpool-summary
zroot: 332G tank: 87G (low)
```

```console
$ zpool-summary
zroot: 332G (ERRORS) tank: 372G
```


## License

See [LICENSE](LICENSE). Copyright © 2022 [Mikael Simonsson](https://mikaelsimonsson.com).


[buildtool]: https://github.com/snncpp/build-tool
[snncore]: https://github.com/snncpp/snn-core
