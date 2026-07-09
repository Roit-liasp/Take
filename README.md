## [En]
# Take

A minimal build system inspired by Make, configured via a simple TOML, in C++17.

## Features

- TOML-based configuration - human-readable and easy to edit
- Variable substitution - define variables in [vars] and use them as ${VAR}
- Dependencies - a function can depend on other functions (e.g. deps = ["build"])
- OS-specific commands - use run_linux, run_macos, run_windows, or run_unix
- Directory switching - run commands in a specific directory with dir = "..."
- Environment variables - access them via ${env:VARNAME}
- Dry-run mode - preview commands without executing (take -n)
- Quiet mode - suppress output (take -q)

## Installation

```
make
sudo make install
```

Or simply copy the take binary to a directory in your $PATH.

## License

BSD 2-Clause License. See LICENSE.md.

---

## [Ru]
# Take

Минимальная система сборки, вдохновлённая Make, но настраиваемая через простой TOML, на C++17.



## Возможности

- Конфигурация в TOML - понятный и легко редактируемый формат
- Подстановка переменных - определяйте переменные в [vars] и используйте как ${VAR}
- **Зависимости** - функция может зависеть от других функций (например, deps = ["build"])
- **Команды под ОС** - используйте run_linux, run_macos, run_windows или run_unix
- **Смена директории** - выполняйте команды в указанной директории через dir = "..."
- **Переменные окружения** - обращайтесь к ним через ${env:VARNAME}
- **Режим пробного запуска** - показать команды без выпполнения (take -n)
- **Тихий режим** - подавить вывод (take -q)

## Установка

```
make
sudo make install
```

Или просто скопируйте бинарник take в директорию из $PATH.

## Лицензия

BSD 2-Clause License. См. [LICENSE.md](LICENSE.md).
