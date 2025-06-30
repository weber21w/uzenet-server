# uzenet-score

**Purpose:**  
`uzenet-score` is a Uzenet-compatible server designed to store and retrieve high scores for Uzebox games over the network. It provides a simple and lightweight interface that allows games to submit new scores and query top entries for leaderboards, either globally or per-user.

---

## Features

- **Score submission**: Games can post new scores using a `Submit <gameid> <score> <username>` command.
- **Leaderboard retrieval**: Clients can request the top N scores for a specific game using `Top <gameid> <count>`.
- **Per-user score lookup**: Retrieve scores by a particular player for a specific game.
- **Simple text protocol**: Optimized for 8-bit clients with line-based, human-readable commands.
- **Optional guest restriction**: Guest users (e.g. user ID 000000) can be restricted from submitting scores.
- **File-backed storage**: All scores are stored in text files or simple databases on disk, making it lightweight and fast.
- **Logging**: Score submissions and retrievals are logged using `syslog`.

---

## Protocol Example

```
Submit NEWGME01 8453 cooldude
OK

Top NEWGME01 3
1. cooldude 8453
2. player2 7300
3. player3 7020
```

---

## Installation

```bash
make
sudo make install
```

To remove:

```bash
sudo make uninstall
```

---

## Deployment Notes

- The server listens on a dedicated TCP port (e.g., 38031).
- Clients must connect and send a command within a short timeout (e.g., 3 seconds).
- All incoming commands must be terminated by `\n`.

---

## Future Features

- Score verification using game-side checksums.
- Periodic backup of score files.
- Support for additional metadata (date/time, country, platform).
- Optional WebSocket or HTTPS frontend for visual leaderboard browsing.

---

## Security & Abuse Prevention

- All guest accounts can be limited or excluded from posting.
- A fail2ban-compatible logging format is planned for abuse attempts.
- Basic username sanitation and filtering of injection attempts is in place.

---

## License

MIT License. See `LICENSE.txt` for details.
