# Language Translator (C# website)

This is the ASP.NET Core version of the translator. It keeps the original
language choices, translation page, detected-language label, and word meanings.
The translation endpoint uses Google's public translate endpoint; it does not
require an API key, but it is not an official or guaranteed service.

## Run on your computer

Install the .NET 8 SDK, then from the repository root run:

```sh
dotnet run --project web
```

Open the local address printed in the terminal (usually `http://localhost:5000`
or `https://localhost:5001`).

## Publish free for your teacher

One beginner-friendly option is a free Render web service. Free-plan limits and
availability can change; free services may sleep when idle, so the first visit
after a while can take a little longer.

1. Put this project on GitHub (create a repository and push your project files).
2. Sign in to [Render](https://render.com/) and choose **New → Web Service**.
3. Connect the GitHub repository.
4. Set **Root Directory** to `web` and choose **Docker** as the runtime. Set
   **Dockerfile Path** to `Dockerfile` (paths are relative to the root directory).
5. Choose the free instance plan and create the service.
6. Wait for the deployment to finish, then open the `onrender.com` address
   Render shows you and share that link with your teacher.

The site must remain deployed while your teacher visits it. The app forwards
translation text to Google's translation endpoint, so do not enter private or
sensitive text. Availability and free hosting plans are controlled by their
providers and can change.

## API

`POST /api/translate` accepts JSON such as:

```json
{ "text": "hello", "source": "en", "target": "fr" }
```

Successful responses contain `translated`, `detected`, and `meaning`.
