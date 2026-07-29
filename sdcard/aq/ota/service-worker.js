'use strict';

const CACHE_PREFIX = 'aquacyd-gateway-shell-';
const CACHE_VERSION = new URL(self.location.href).searchParams.get('v') || 'v1';
const SHELL_CACHE = `${CACHE_PREFIX}${CACHE_VERSION}`;
const SHELL_ASSETS = [
    '/',
    '/index.html',
    '/settings.html',
    '/manifest.webmanifest',
    '/aquacyd-icon-192.png',
    '/aquacyd-icon-512.png',
    '/theme-bootstrap.js',
    '/style.css',
    '/charts.css',
    '/app-core.js',
    '/app-init.js',
    '/chart-engine.js',
    '/charts.js',
    '/dashboard.js',
    '/gateway-pwa.js',
    '/logs.js',
    '/ota.js',
    '/relays-wizard.js',
    '/schedules.js',
    '/settings.js',
    '/theme.js'
];
const NETWORK_ONLY_PREFIXES = [
    '/.well-known/',
    '/api/',
    '/update',
    '/settime',
    '/download',
    '/history.csv'
];
const SHELL_PATHS = new Set(
    SHELL_ASSETS.map((asset) => new URL(asset, self.location.origin).pathname)
);

function isNetworkOnly(url) {
    return NETWORK_ONLY_PREFIXES.some((prefix) => url.pathname.startsWith(prefix));
}

self.addEventListener('install', (event) => {
    event.waitUntil(
        caches.open(SHELL_CACHE)
            .then((cache) => cache.addAll(SHELL_ASSETS))
            .then(() => self.skipWaiting())
    );
});

self.addEventListener('activate', (event) => {
    event.waitUntil(
        caches.keys()
            .then((keys) => Promise.all(
                keys
                    .filter((key) => key.startsWith(CACHE_PREFIX) && key !== SHELL_CACHE)
                    .map((key) => caches.delete(key))
            ))
            .then(() => self.clients.claim())
    );
});

self.addEventListener('fetch', (event) => {
    const request = event.request;
    if (request.method !== 'GET') {
        return;
    }

    const url = new URL(request.url);
    if (url.origin !== self.location.origin || isNetworkOnly(url)) {
        return;
    }

    if (request.mode === 'navigate') {
        event.respondWith(
            fetch(request)
                .then((response) => {
                    if (response.ok) {
                        const copy = response.clone();
                        event.waitUntil(
                            caches.open(SHELL_CACHE)
                                .then((cache) => cache.put('/index.html', copy))
                        );
                    }
                    return response;
                })
                .catch(() => caches.match('/index.html'))
        );
        return;
    }

    if (!SHELL_PATHS.has(url.pathname)) {
        return;
    }

    event.respondWith(
        caches.match(request, { ignoreSearch: true }).then((cached) => {
            if (cached) {
                return cached;
            }
            return fetch(request).then((response) => {
                if (!response.ok || response.type !== 'basic') {
                    return response;
                }
                const copy = response.clone();
                event.waitUntil(
                    caches.open(SHELL_CACHE).then((cache) => cache.put(request, copy))
                );
                return response;
            });
        })
    );
});
