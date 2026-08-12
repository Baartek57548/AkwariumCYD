import {
  appendFile,
  mkdir,
  open,
  readFile,
  readdir,
  rename,
  stat,
  writeFile,
} from 'node:fs/promises';
import path from 'node:path';

const FCM_TOKEN = /^[A-Za-z0-9_:\-.]{32,4096}$/;
const EVENT_INDEX_FILE = 'event-index.json';
const EVENT_INDEX_LIMIT = 10_000;
const EVENT_FILE = /^events-(\d{4}-\d{2}-\d{2})(?:-(\d{3}))?\.jsonl$/;
const DEFAULT_EVENT_FILE_BYTES = 4 * 1024 * 1024;
const MAXIMUM_READABLE_EVENT_FILE_BYTES = 64 * 1024 * 1024;

function dateKey(date) {
  return date.toISOString().slice(0, 10);
}

function safeDeviceDirectory(root, deviceId) {
  return path.join(root, 'devices', deviceId);
}

async function writePrivateJson(filePath, value) {
  const temporary = `${filePath}.${process.pid}.tmp`;
  await writeFile(temporary, `${JSON.stringify(value, null, 2)}\n`, {
    encoding: 'utf8',
    mode: 0o600,
  });
  await rename(temporary, filePath);
}

export class EventStore {
  constructor(rootDirectory, {
    maximumEventFileBytes = DEFAULT_EVENT_FILE_BYTES,
  } = {}) {
    if (
      !Number.isSafeInteger(maximumEventFileBytes)
      || maximumEventFileBytes < 1024
      || maximumEventFileBytes > 16 * 1024 * 1024
    ) {
      throw new RangeError(
        'Limit segmentu historii musi wynosić od 1 KiB do 16 MiB.',
      );
    }
    this.rootDirectory = rootDirectory;
    this.maximumEventFileBytes = maximumEventFileBytes;
    this.eventIndexes = new Map();
    this.writeTails = new Map();
  }

  async initialize(deviceIds) {
    await mkdir(this.rootDirectory, { recursive: true, mode: 0o700 });
    for (const deviceId of deviceIds) {
      const directory = safeDeviceDirectory(this.rootDirectory, deviceId);
      await mkdir(directory, {
        recursive: true,
        mode: 0o700,
      });
      const index = await this.loadEventIndex(deviceId);

      // Rebuild the newest part from the append-only journal. This repairs the
      // only possible crash window: the event reached JSONL, but the compact
      // index had not yet been atomically replaced.
      const recentEvents = await this.latest(deviceId, 200);
      for (const event of recentEvents.reverse()) {
        if (
          typeof event.bootId === 'string'
          && typeof event.eventId === 'string'
          && typeof event.receiptId === 'string'
        ) {
          index.set(
            this.eventKey(event.bootId, event.eventId),
            event.receiptId,
          );
        }
      }
      this.trimEventIndex(index);
      this.eventIndexes.set(deviceId, index);
      await this.saveEventIndex(deviceId, index);
    }
  }

  eventKey(bootId, eventId) {
    return `${bootId}\u0000${eventId}`;
  }

  trimEventIndex(index) {
    while (index.size > EVENT_INDEX_LIMIT) {
      index.delete(index.keys().next().value);
    }
  }

  async loadEventIndex(deviceId) {
    const target = path.join(
      safeDeviceDirectory(this.rootDirectory, deviceId),
      EVENT_INDEX_FILE,
    );
    try {
      const parsed = JSON.parse(await readFile(target, 'utf8'));
      if (parsed.version !== 1 || !Array.isArray(parsed.entries)) {
        return new Map();
      }
      const entries = parsed.entries.filter(
        (entry) => (
          Array.isArray(entry)
          && entry.length === 2
          && typeof entry[0] === 'string'
          && entry[0].length <= 194
          && typeof entry[1] === 'string'
          && entry[1].length <= 96
        ),
      );
      return new Map(entries.slice(-EVENT_INDEX_LIMIT));
    } catch (error) {
      if (error?.code === 'ENOENT' || error instanceof SyntaxError) {
        return new Map();
      }
      throw error;
    }
  }

  async saveEventIndex(deviceId, index) {
    const directory = safeDeviceDirectory(this.rootDirectory, deviceId);
    await mkdir(directory, { recursive: true, mode: 0o700 });
    await writePrivateJson(path.join(directory, EVENT_INDEX_FILE), {
      version: 1,
      entries: [...index.entries()],
      updatedAt: new Date().toISOString(),
    });
  }

  async appendUnique(deviceId, event) {
    return this.runSerialized(
      deviceId,
      () => this.appendUniqueSerialized(deviceId, event),
    );
  }

  async runSerialized(deviceId, action) {
    const previous = this.writeTails.get(deviceId) ?? Promise.resolve();
    const operation = previous
      .catch(() => undefined)
      .then(action);
    this.writeTails.set(deviceId, operation);
    try {
      return await operation;
    } finally {
      if (this.writeTails.get(deviceId) === operation) {
        this.writeTails.delete(deviceId);
      }
    }
  }

  async eventTarget(deviceId, encodedLength) {
    const directory = safeDeviceDirectory(this.rootDirectory, deviceId);
    const date = dateKey(new Date());
    const names = await readdir(directory);
    let segment = 0;
    for (const name of names) {
      const match = EVENT_FILE.exec(name);
      if (match?.[1] === date) {
        segment = Math.max(segment, Number(match[2] ?? 0));
      }
    }
    const fileName = segment === 0
      ? `events-${date}.jsonl`
      : `events-${date}-${String(segment).padStart(3, '0')}.jsonl`;
    let target = path.join(directory, fileName);
    let currentBytes = 0;
    try {
      currentBytes = (await stat(target)).size;
    } catch (error) {
      if (error?.code !== 'ENOENT') throw error;
    }
    if (
      currentBytes > 0
      && currentBytes + encodedLength > this.maximumEventFileBytes
    ) {
      segment += 1;
      if (segment > 999) {
        throw new Error('Dzienny limit segmentów historii został przekroczony.');
      }
      target = path.join(
        directory,
        `events-${date}-${String(segment).padStart(3, '0')}.jsonl`,
      );
    }
    return target;
  }

  async appendUniqueSerialized(deviceId, event) {
    const index = this.eventIndexes.get(deviceId);
    if (index === undefined) {
      throw new Error(`Magazyn urządzenia ${deviceId} nie został zainicjalizowany.`);
    }
    const key = this.eventKey(event.bootId, event.eventId);
    const existingReceipt = index.get(key);
    if (existingReceipt !== undefined) {
      return { duplicate: true, receiptId: existingReceipt };
    }

    const directory = safeDeviceDirectory(this.rootDirectory, deviceId);
    await mkdir(directory, { recursive: true, mode: 0o700 });
    const encoded = `${JSON.stringify(event)}\n`;
    const target = await this.eventTarget(
      deviceId,
      Buffer.byteLength(encoded, 'utf8'),
    );
    await appendFile(target, encoded, {
      encoding: 'utf8',
      mode: 0o600,
    });
    index.set(key, event.receiptId);
    this.trimEventIndex(index);
    await this.saveEventIndex(deviceId, index);
    return { duplicate: false, receiptId: event.receiptId };
  }

  async latest(deviceId, requestedLimit = 50) {
    const limit = Math.max(1, Math.min(200, Number(requestedLimit) || 50));
    const directory = safeDeviceDirectory(this.rootDirectory, deviceId);
    let names;
    try {
      names = (await readdir(directory))
        .filter((name) => EVENT_FILE.test(name))
        .sort((left, right) => {
          const leftMatch = EVENT_FILE.exec(left);
          const rightMatch = EVENT_FILE.exec(right);
          const dateOrder = leftMatch[1].localeCompare(rightMatch[1]);
          if (dateOrder !== 0) return dateOrder;
          return Number(leftMatch[2] ?? 0) - Number(rightMatch[2] ?? 0);
        })
        .reverse();
    } catch (error) {
      if (error?.code === 'ENOENT') {
        return [];
      }
      throw error;
    }

    const result = [];
    for (const name of names) {
      const file = path.join(directory, name);
      const metadata = await stat(file);
      if (metadata.size > MAXIMUM_READABLE_EVENT_FILE_BYTES) {
        throw new Error(`Plik historii ${name} przekracza bezpieczny limit.`);
      }
      const lines = (await readFile(file, 'utf8'))
        .split('\n')
        .filter(Boolean)
        .reverse();
      for (const line of lines) {
        try {
          result.push(JSON.parse(line));
        } catch {
          continue;
        }
        if (result.length >= limit) {
          return result;
        }
      }
    }
    return result;
  }

  async loadPushTokens(deviceId) {
    const target = path.join(
      safeDeviceDirectory(this.rootDirectory, deviceId),
      'push-tokens.json',
    );
    try {
      const file = await open(target, 'r');
      try {
        const parsed = JSON.parse(await file.readFile('utf8'));
        return Array.isArray(parsed.tokens)
          ? parsed.tokens.filter((token) => FCM_TOKEN.test(token))
          : [];
      } finally {
        await file.close();
      }
    } catch (error) {
      if (error?.code === 'ENOENT' || error instanceof SyntaxError) {
        return [];
      }
      throw error;
    }
  }

  async addPushToken(deviceId, token) {
    if (typeof token !== 'string' || !FCM_TOKEN.test(token)) {
      throw new TypeError('Nieprawidłowy token FCM.');
    }
    return this.runSerialized(deviceId, async () => {
      const current = await this.loadPushTokens(deviceId);
      const next = [token, ...current.filter((value) => value !== token)]
        .slice(0, 20);
      await this.savePushTokens(deviceId, next);
      return next.length;
    });
  }

  async removePushToken(deviceId, token) {
    if (typeof token !== 'string' || !FCM_TOKEN.test(token)) {
      throw new TypeError('Nieprawidłowy token FCM.');
    }
    return this.runSerialized(deviceId, async () => {
      const current = await this.loadPushTokens(deviceId);
      const next = current.filter((value) => value !== token);
      await this.savePushTokens(deviceId, next);
      return next.length;
    });
  }

  async savePushTokens(deviceId, tokens) {
    const directory = safeDeviceDirectory(this.rootDirectory, deviceId);
    await mkdir(directory, { recursive: true, mode: 0o700 });
    await writePrivateJson(path.join(directory, 'push-tokens.json'), {
      version: 1,
      tokens,
      updatedAt: new Date().toISOString(),
    });
  }
}
