# Клиент-сервер на C и инструментарий для автоматизации разблокировки зашифрованного диска

## Сборка

Зависимости: libsodium, gcc (или другой компилятор).

Для сборки используйте *build.sh*.

## Использование

Сгенерируйте пару ключей с помощью generate_keys

`./generate_keys`
```
Public Key: e77bad506ea65ffa098e215a630720f707027bc9f2f17fdad9a9073c2eee4356
Secret Key: 18ad81af2f5fa823e8497c03b29da288679c543f8e45a7b825e0a7c1f791c805
```

Запустите сервер, который будет ждать пока ему не отправят ключ

`./vxserver -p 5000 -s 18ad81af2f5fa823e8497c03b29da288679c543f8e45a7b825e0a7c1f791c805 -o e77bad506ea65ffa098e215a630720f707027bc9f2f17fdad9a9073c2eee4356 > master_key.txt`

где -p указывает порт, -s приватный ключ, -o публичный ключ. После получения сообщение будет записано в master_key.txt.

Для отправки сообщения в зашифрованном виде используйте

`./vxclient -i 127.0.0.1 -p 5000 -o e77bad506ea65ffa098e215a630720f707027bc9f2f17fdad9a9073c2eee4356 -m "Hello, World!"`

где -i указывает айпи, -p порт, -o публичный ключ, -m сообщение (мастер ключ зашифрованного диска).

### После получения мастер ключа

Используйте *decrypt_and_open.py* для разблокирования зашифрованного диска. По необходимости поменяйте константы в начале программы. 

`python decrypt_and_open.py`
