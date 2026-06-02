// TOFU pins are stored in localStorage — they're per-device trust decisions, not secrets.

const TOFU_KEY      = 'securemsg_tofu';
const SIGN_TOFU_KEY = 'securemsg_sign_tofu';

export function loadTofu() {
  const raw = localStorage.getItem(TOFU_KEY);
  return raw ? JSON.parse(raw) : {};
}

export function saveTofu(tofu) {
  localStorage.setItem(TOFU_KEY, JSON.stringify(tofu));
}

export function loadSignTofu() {
  const raw = localStorage.getItem(SIGN_TOFU_KEY);
  return raw ? JSON.parse(raw) : {};
}

export function saveSignTofu(signTofu) {
  localStorage.setItem(SIGN_TOFU_KEY, JSON.stringify(signTofu));
}
