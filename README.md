hexitor
=======

A terminal hex editor.

![A screenshot](https://s3.amazonaws.com/briansteffens/hexitor.png)

# Installation

If you're using Arch, hexitor is available on the AUR:
<https://aur.archlinux.org/packages/hexitor/>

Otherwise, make sure you have gcc, make, git, and ncurses installed and then:

```bash
git clone https://github.com/briansteffens/hexitor
cd hexitor
make
sudo make install
```

To uninstall:

```bash
sudo make uninstall
```

# Installation without root

If you don't have root access and there is not libncurses installed,
first install ncurses

```bash
wget https://invisible-mirror.net/archives/ncurses/ncurses-6.2.tar.gz
tar -xzf ncurses-6.2.tar.gz
cd ncurses-6.2
./configure --prefix=$HOME/ncurses
make
make install
cd ..
rm -rf ncurses-6.2*
```

Then compile and install hexitor

```bash
make LDIR=$HOME/ncurses
make DESTDIR=$HOME/bin install
```

to uninstall

```bash
make DESTDIR=$HOME/bin uninstall
```

# Usage

### Opening a file

```bash
hexitor <some_file>
```

### Movement

- Use the arrow keys or *hjkl* to move the cursor around the editor.
- Page up, page down, home, and end work as expected.
- Type ```:123``` and hit enter to move to the 123rd byte in the file.
- Use ```q``` and ```w``` to move back and forth one byte at a time.
- Use ```gg``` to move to the beginning of the buffer.
- Use ```G``` to move to the end of the buffer.

### Searching

Type a forward-slash followed by a series of hex digits to search for that
sequence of bytes in the buffer. For example, ```/05 0f``` would search for
the bytes ```05 0f```. Use ```n``` to jump to the next occurrence of the
search term and use ```N``` to jump to the previous occurrence of the search
term.

### Editing bytes

The keys 0-9 and a-f will overwrite the current nibble (half-byte).

### Saving changes

- Type ```:w``` and hit enter to save the file.
- Type ```:w <some_other_file>``` and hit enter to save changes to a different
  file.

### Quitting

Type ```:q``` and hit enter to quit.
