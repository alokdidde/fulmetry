// Mirrors T3 Code packages/provider-kiro/src/settings.ts (KiroSettings). In the T3 fork this becomes a
// makeProviderSettingsSchema(...) Effect Schema; here it is a plain decoder so the package needs no Effect.

export interface RavenSettings {
  /** Fulmetry makes Raven the default agent, so it is on by default (Kiro and Grok are off). */
  readonly enabled: boolean;
  /** The `raven` executable. Raven's pyproject.toml installs it as `raven = "raven.cli.entry:run"`. */
  readonly binaryPath: string;
  /** Passed as `raven acp --config <path>`; empty means the instance $RAVEN_HOME names. */
  readonly configPath: string;
  /** Extra models the user typed in; Raven itself advertises its model list via session config options. */
  readonly customModels: ReadonlyArray<string>;
}

export const DEFAULT_RAVEN_SETTINGS: RavenSettings = {
  enabled: true,
  binaryPath: "raven",
  configPath: "",
  customModels: [],
};

export function decodeRavenSettings(input: unknown): RavenSettings {
  const raw = (typeof input === "object" && input !== null ? input : {}) as Record<string, unknown>;
  const str = (key: string, fallback: string) => {
    const value = raw[key];
    return typeof value === "string" && value.trim().length > 0 ? value.trim() : fallback;
  };
  const models = Array.isArray(raw.customModels)
    ? raw.customModels.filter((m): m is string => typeof m === "string" && m.trim().length > 0)
    : [];
  return {
    enabled: typeof raw.enabled === "boolean" ? raw.enabled : DEFAULT_RAVEN_SETTINGS.enabled,
    binaryPath: str("binaryPath", DEFAULT_RAVEN_SETTINGS.binaryPath),
    configPath: str("configPath", DEFAULT_RAVEN_SETTINGS.configPath),
    customModels: models,
  };
}
