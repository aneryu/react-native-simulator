globalThis.RNS_DEVELOPMENT_SESSION = {
  loaded: true,
  startedAt: Date.now(),
};

setInterval(() => {
  globalThis.RNS_DEVELOPMENT_SESSION.ticks =
    (globalThis.RNS_DEVELOPMENT_SESSION.ticks || 0) + 1;
}, 25);
