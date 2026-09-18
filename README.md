# Canvas Due Soon

Canvas Due Soon is a small C web app built with the Mongoose framework. It loads a student's active Canvas courses, lets the student choose a course from a form, and displays that course's assignments with due dates, points, and submission status in a readable table.

## Setup Instructions

1. Install [MSYS2](https://www.msys2.org/) on Windows and open the **MSYS2 UCRT64** terminal. Install GCC with `pacman -S mingw-w64-ucrt-x86_64-gcc`.
2. Clone this repository and change into its directory.
3. Copy `.env.example` to `.env`.
4. In Canvas, create an access token under **Account → Settings → Approved Integrations** and put it in `.env` as `CANVAS_API_TOKEN=...`. Never commit `.env`.
5. Build the app with `gcc -DMG_TLS=3 -o server.exe main.c mongoose.c -lws2_32` (the flag enables Mongoose's built-in TLS client).
6. Run `./server.exe` and open <http://localhost:8080>.
7. Choose a course and click **Show assignments**. Stop the server with `Ctrl+C`.

The token is read from the environment first and then from `.env`. If Canvas rejects it, revoke it immediately and create a replacement. The app never sends the token to the browser.

## API Endpoints Used

| Method | Endpoint | Purpose |
| --- | --- | --- |
| GET | `/api/v1/courses?enrollment_state=active` | Loads the student's active courses for the dropdown. |
| GET | `/api/v1/courses/:course_id/assignments?order_by=due_at&include[]=submission` | Loads assignments, due dates, points, links, and the student's submission status for the selected course. |

Both requests use `Authorization: Bearer <token>` over HTTPS. Canvas list endpoints default to ten records per page. The app requests 100 records and still follows every absolute `rel="next"` URL in the case that more pages exist.

## Error Handling

Missing configuration, network failures, and Canvas HTTP errors are shown as a friendly error page. HTTP 401, 403, and other status codes are explained separately so the student knows whether to replace a token, check permissions, or retry later.

## Reflection

The most useful lesson was that a REST API response is not necessarily complete just because it is valid JSON. Canvas puts pagination in the HTTP `Link` header, so the client has to inspect headers and follow the opaque next URL. I also learned that bearer authentication belongs in an HTTP header and that HTTPS is important because an access token is effectively a password.

The most challenging part was coordinating two asynchronous HTTPS requests in a small C program. Mongoose handles the sockets and event loop, while the app stores course and assignment data in simple structs before rendering the page. Parsing only the fields needed by the UI made the code easier to understand than dumping raw JSON.

With more time, I would add cached responses, a cross-course “due this week” view, unit tests for pagination and HTML escaping, and OAuth instead of asking each student to manage a personal token. I would also add a recorded demo GIF showing course selection and the assignment table in action.

## Files

- `main.c` – Mongoose web server, Canvas HTTPS client, pagination, JSON parsing, and HTML rendering.
- `mongoose.c` / `mongoose.h` – Mongoose framework source.
- `.env.example` – Safe configuration template.
- `.gitignore` – Keeps tokens and build artifacts out of Git.