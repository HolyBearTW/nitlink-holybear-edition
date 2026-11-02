// Derived from Harbor, commit 2c243bb; modified for NitLink. GPL-3.0. See SOURCE.md.
using System.Text.Json.Serialization;

namespace NitLinkSetup.Localization;

[JsonSourceGenerationOptions(PropertyNameCaseInsensitive = true)]
[JsonSerializable(typeof(LanguageOption[]))]
[JsonSerializable(typeof(Dictionary<string, string>))]
internal partial class LocalizationJsonContext : JsonSerializerContext;
