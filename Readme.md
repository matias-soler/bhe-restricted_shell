

```
m restricted_shell
```


```
user@aosp:~/aosp$ adb push out/target/product/rpi5/system/bin/restricted_shell /tmp
```

```
mount -o remount -o rw /
cp /tmp/restricted_shell /system/bin/restricted_shell
```