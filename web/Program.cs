using System.Text;
using System.Text.Json;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddHttpClient("translator", client =>
{
    client.Timeout = TimeSpan.FromSeconds(20);
    client.DefaultRequestHeaders.UserAgent.ParseAdd(
        "Mozilla/5.0 (compatible; Translator.Web/1.0)");
    client.DefaultRequestHeaders.AcceptLanguage.ParseAdd("en-US,en;q=0.9");
});

var app = builder.Build();
app.UseDefaultFiles();
app.UseStaticFiles();

var languages = new HashSet<string>(StringComparer.Ordinal)
{
    "auto", "ceb", "en", "fr", "tl"
};
var targets = new HashSet<string>(languages, StringComparer.Ordinal);
targets.Remove("auto");

app.MapPost("/api/translate", async (
    TranslateRequest? request,
    IHttpClientFactory clients,
    CancellationToken cancellationToken) =>
{
    var text = request?.Text?.Trim();
    var source = request?.Source ?? "auto";
    var target = request?.Target ?? "en";

    if (string.IsNullOrEmpty(text))
        return Results.BadRequest(new { error = "Text is required" });
    if (!languages.Contains(source))
        return Results.BadRequest(new { error = $"Unsupported source language: {source}" });
    if (!targets.Contains(target))
        return Results.BadRequest(new { error = $"Unsupported target language: {target}" });

    var url = "https://translate.googleapis.com/translate_a/single" +
              $"?client=dict-chrome-ex&sl={Uri.EscapeDataString(source)}" +
              $"&tl={Uri.EscapeDataString(target)}&dt=t&dt=bd&q={Uri.EscapeDataString(text)}";
    try
    {
        using var response = await clients.CreateClient("translator")
            .GetAsync(url, cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            return Results.Json(
                new { error = $"Translation service returned HTTP {(int)response.StatusCode}" },
                statusCode: StatusCodes.Status502BadGateway);
        }

        await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken);
        using var document = await JsonDocument.ParseAsync(stream, cancellationToken: cancellationToken);
        var root = document.RootElement;
        var translated = ExtractTranslation(root);
        if (string.IsNullOrEmpty(translated))
        {
            return Results.Json(new { error = "Empty translation" },
                statusCode: StatusCodes.Status502BadGateway);
        }

        var detected = root.ValueKind == JsonValueKind.Array &&
                      root.GetArrayLength() > 2 &&
                      root[2].ValueKind == JsonValueKind.String
            ? root[2].GetString()
            : null;

        return Results.Json(new
        {
            translated,
            detected = string.IsNullOrEmpty(detected) ? source : detected,
            meaning = ExtractMeaning(root)
        });
    }
    catch (HttpRequestException)
    {
        return Results.Json(new { error = "Could not reach the translation service" },
            statusCode: StatusCodes.Status502BadGateway);
    }
    catch (TaskCanceledException) when (!cancellationToken.IsCancellationRequested)
    {
        return Results.Json(new { error = "Translation service timed out" },
            statusCode: StatusCodes.Status502BadGateway);
    }
    catch (JsonException)
    {
        return Results.Json(new { error = "Invalid response from translation service" },
            statusCode: StatusCodes.Status502BadGateway);
    }
});

app.Run();

static string ExtractTranslation(JsonElement root)
{
    if (root.ValueKind != JsonValueKind.Array || root.GetArrayLength() == 0 ||
        root[0].ValueKind != JsonValueKind.Array)
        return "";

    var result = new StringBuilder();
    foreach (var segment in root[0].EnumerateArray())
    {
        if (segment.ValueKind == JsonValueKind.Array &&
            segment.GetArrayLength() > 0 &&
            segment[0].ValueKind == JsonValueKind.String)
            result.Append(segment[0].GetString());
    }
    return result.ToString();
}

static string ExtractMeaning(JsonElement root)
{
    if (root.ValueKind != JsonValueKind.Array || root.GetArrayLength() < 2 ||
        root[1].ValueKind != JsonValueKind.Array)
        return "";

    var lines = new List<string>();
    foreach (var entry in root[1].EnumerateArray())
    {
        if (entry.ValueKind != JsonValueKind.Array || entry.GetArrayLength() < 3)
            continue;

        var word = entry.GetArrayLength() > 3 && entry[3].ValueKind == JsonValueKind.String
            ? entry[3].GetString()
            : null;
        var partOfSpeech = entry[0].ValueKind == JsonValueKind.String
            ? entry[0].GetString()
            : null;
        var header = $"{word}{(string.IsNullOrEmpty(partOfSpeech) ? "" : $"  ({partOfSpeech})")}".Trim();
        if (header.Length == 0)
            continue;

        lines.Add($"• {header}");
        if (entry[2].ValueKind != JsonValueKind.Array)
            continue;
        foreach (var definition in entry[2].EnumerateArray().Take(4))
        {
            if (definition.ValueKind == JsonValueKind.Array &&
                definition.GetArrayLength() > 0 &&
                definition[0].ValueKind == JsonValueKind.String)
            {
                var meaning = definition[0].GetString();
                if (!string.IsNullOrEmpty(meaning))
                    lines.Add($"    - {meaning}");
            }
        }
    }
    return string.Join('\n', lines);
}

record TranslateRequest(string? Text, string? Source, string? Target);
