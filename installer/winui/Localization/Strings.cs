// Derived from Harbor, commit 2c243bb; modified for NitLink. GPL-3.0. See SOURCE.md.
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization.Metadata;

namespace NitLinkSetup.Localization;

public static class Strings
{
    public static IReadOnlyList<LanguageOption> Languages { get; } = ReadResource("languages", LocalizationJsonContext.Default.LanguageOptionArray);
    public static IReadOnlyList<string> SupportedLanguages { get; } = Languages.Select(language => language.Id).ToArray();
    public static string Language { get; private set; } = "en-US";
    private static Dictionary<string, string> catalog = Load(Language);

    public static void Initialize(string preference, IEnumerable<string> systemLanguages)
    {
        Language = ResolveLanguage(preference, systemLanguages);
        catalog = Load(Language);
        var culture = CultureInfo.GetCultureInfo(Language);
        CultureInfo.DefaultThreadCurrentCulture = CultureInfo.CurrentCulture = culture;
        CultureInfo.DefaultThreadCurrentUICulture = CultureInfo.CurrentUICulture = culture;
    }

    public static string ResolveLanguage(string preference, IEnumerable<string> systemLanguages)
    {
        if (preference.Length > 0)
        {
            if (!SupportedLanguages.Contains(preference))
                throw new ArgumentException("Unsupported language", nameof(preference));
            return preference;
        }
        foreach (var language in systemLanguages)
        {
            var exact = SupportedLanguages.FirstOrDefault(supported => string.Equals(supported, language, StringComparison.OrdinalIgnoreCase));
            if (exact is not null) return exact;
            var parts = language.ToLowerInvariant().Split('-');
            var family = SupportedLanguages.FirstOrDefault(supported => supported.Split('-')[0] == parts[0]);
            if (parts[0] != "zh" && family is not null) return family;
            if (parts[0] == "en")
                return "en-US";
            if (parts[0] == "zh")
            {
                if (parts.Contains("hant"))
                    return "zh-TW";
                if (parts.Contains("hans"))
                    return "zh-CN";
                return parts.Intersect(["tw", "hk", "mo"]).Any() ? "zh-TW" : "zh-CN";
            }
        }
        return "en-US";
    }

    public static string Get(string key) => catalog[key];
    public static string Format(string key, params object?[] arguments) => string.Format(CultureInfo.CurrentCulture, Get(key), arguments);

    private static Dictionary<string, string> Load(string language)
        => ReadResource(language, LocalizationJsonContext.Default.DictionaryStringString);

    private static T ReadResource<T>(string name, JsonTypeInfo<T> typeInfo)
    {
        using var stream = typeof(Strings).Assembly.GetManifestResourceStream($"NitLinkSetup.Localization.{name}.json")!;
        return JsonSerializer.Deserialize(stream, typeInfo)!;
    }
}
