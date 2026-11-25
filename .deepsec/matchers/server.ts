import { regexMatcher, type MatcherPlugin } from "deepsec/config";

export const serverController: MatcherPlugin = {
  slug: "skyblip-controller",
  description: "Every Rails controller, since most of the server's ingress inherits allow_unauthenticated_access from a namespace base class",
  noiseTier: "normal",
  filePatterns: ["app/controllers/**/*.rb"],
  examples: ["class Briefing::DevicesController < Briefing::ApplicationController"],
  match(content) {
    return regexMatcher("skyblip-controller", [{ regex: /^class\s+[\w:]+Controller\s*</gm, label: "Rails controller" }], content);
  },
};

export const serverTrustModel: MatcherPlugin = {
  slug: "skyblip-trust-model",
  description: "Models and tools that verify signatures, nonces, OAuth codes and tokens or unit keys, or take what browsers and devices send",
  noiseTier: "normal",
  filePatterns: [
    "app/models/oauth.rb", "app/models/oauth/**/*.rb",
    "app/models/activation.rb", "app/models/activation/**/*.rb",
    "app/models/attestation.rb", "app/models/attestation/**/*.rb",
    "app/models/identity/**/*.rb",
    "app/models/unit.rb", "app/models/unit/**/*.rb",
    "app/models/capture.rb", "app/models/capture/**/*.rb",
    "app/models/event.rb", "app/models/event/**/*.rb",
    "app/models/release.rb", "app/models/release/**/*.rb",
    "app/models/concerns/single_use.rb", "app/models/session.rb", "app/models/user.rb",
    "app/tools/**/*.rb", "lib/middleware/**/*.rb", "config/routes.rb", "config/routes/**/*.rb",
  ],
  examples: ["class OAuth::Token < ApplicationRecord"],
  match(content) {
    return regexMatcher("skyblip-trust-model", [{ regex: /^\s*(class|module)\s+[\w:]+|^Rails\.application\.routes/gm, label: "trust-path code" }], content);
  },
};
