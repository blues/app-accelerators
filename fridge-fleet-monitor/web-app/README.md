# 🐦 Sparrow Reference Web App

An example web application to configure and view sensor data from Blues Wireless Sparrow devices.

- [🐦 Sparrow Reference Web App](#-sparrow-reference-web-app)
  - [Development Overview](#development-overview)
    - [Clone This Repository](#clone-this-repository)
    - [Dependencies](#dependencies)
      - [(Recommended) Visual Studio Code Dev Container](#recommended-visual-studio-code-dev-container)
      - [(Not Recommended) Dependencies without VS Code](#not-recommended-dependencies-without-vs-code)
    - [Configuration (Environment Variables)](#configuration-environment-variables)
      - [HUB_AUTH_TOKEN](#hub_auth_token)
      - [HUB_PROJECTUID](#hub_projectuid)
      - [POSTGRES\_\* and DATABASE_URL](#postgres_-and-database_url)
    - [Routing](#routing)
    - [Create a tunnel to a server running the reference app](#create-a-tunnel-to-a-server-running-the-reference-app)
      - [Localtunnel](#localtunnel)
      - [Ngrok](#ngrok)
    - [Set up a Notehub route to your tunnel](#set-up-a-notehub-route-to-your-tunnel)
    - [Database](#database)
      - [Create the database](#create-the-database)
      - [Custom PostgreSQL Server](#custom-postgresql-server)
      - [Troubleshooting Sparrow getting data from Postgres](#troubleshooting-sparrow-getting-data-from-postgres)
    - [Web App Development](#web-app-development)
    - [Bulk Data Import](#bulk-data-import)
  - [Cloud Deployment](#cloud-deployment)
    - [Deploy on Netlify (recommended)](#deploy-on-netlify-recommended)
    - [Deploy on Vercel](#deploy-on-vercel)
    - [Deploy on Microsoft Azure Cloud](#deploy-on-microsoft-azure-cloud)
  - [Security](#security)
  - [Testing](#testing)
    - [Testing with Jest](#testing-with-jest)
    - [Testing with Cypress](#testing-with-cypress)
  - [Support](#support)

## Development Overview

To get started you need to:

- [Create a Notehub account](https://dev.blues.io/notehub/notehub-walkthrough/) if you don't already have one.
- [Create a Notehub project](https://dev.blues.io/notehub/notehub-walkthrough/#create-a-new-project)
  for your Sparrow devices.
- [Set up a Sparrow Gateway and one or more sensors](https://bluesinc.atlassian.net/wiki/spaces/SPAR/pages/7733505/Sparrow+Quickstart+Guide).
- [Clone this repository](#clone-this-repository) to your local development
  environment.
- Install the project’s development [dependencies](#dependencies).
- Configure this starter web app via [environment variables](#configuration-environment-variables).
- Start up a local [database](#database).
- Set up event [routing](#routing).
- Launch the Sparrow Reference Web App app in [development
  mode](#web-app-development).

### Clone This Repository

To start using the Sparrow Reference Web App you must clone this repository to
your local development machine. You can do this with `git clone`.

```
git clone https://github.com/blues/sparrow-reference-web-app.git
```

With your local project downloaded, you’ll next want to open up the
`sparrow-reference-web-app` folder in your text editor or IDE of choice. Once
you have the project open in your editor you’re ready to configure the project’s
environment variables.

### Dependencies

#### (Recommended) Visual Studio Code Dev Container

Although this project is designed for development on Linux,
[Visual Studio Code](https://code.visualstudio.com/) (VS Code) can quickly create a
Linux ["Dev Container"](https://code.visualstudio.com/docs/remote/containers) on Windows, Mac, or Linux. To use this workflow **you must install both VS Code and Docker**, if
you haven’t already.

- [Install VS Code](https://code.visualstudio.com/)
- [Install Docker](https://docs.docker.com/get-docker/)

Before continuing, additionally make sure Docker is running, which you can do by
checking the following.

- **Windows**: Check for the docker (whale) icon in the system tray.
- **Linux/Mac**: Run the command `docker run hello-world` from your terminal. If everything is working correctly you’ll see a confirmation message.

When you open the folder containing this README in VS Code you will see boxes that
prompt you to install the extension **Remote - Containers**, and then to “Reopen in Container”. Do both.

![install Remote Containers](readme-install-remote-containers-extention.png)

![reopen in container](readme-reopen-in-container.png)

The Dev Container will automatically install Linux and the project dependencies,
no matter which kind of operating system your development machine uses.

As a final step, open a Linux terminal in VS Code, as you’ll need it to run commands throughout the rest of this guide:

- VS Code > Menus > Terminal > New Terminal

#### (Not Recommended) Dependencies without VS Code

If you choose **not** to use a Dev Container in VS Code, you can install the
project dependencies as follows.

The Sparrow Reference Web App uses [Node.js](https://nodejs.org/en/) as a
runtime, [Yarn](https://yarnpkg.com/) as a package manager, and
[Volta](https://volta.sh/) as a way of enforcing consistent versions of all
JavaScript-based tools. You can install these dependencies by following the
steps below.

1. Install Volta by following its installation
   [instructions](https://docs.volta.sh/guide/getting-started).
2. Run the command below in a terminal to install the appropriate versions of
   both Node.js and Yarn.
   ```
   volta install node yarn
   ```
3. Navigate to the root of the Sparrow Reference Web App in your terminal or
   command prompt and run `yarn install`, which installs the starter’s npm
   dependencies.
   ```
   yarn install
   ```
4. Install the [PostgreSQL](https://www.postgresql.org/download/) database engine.

### Configuration (Environment Variables)

The Sparrow Reference Web App uses a series of environment variables to store
project-specific configuration. You _must_ define your own values for these
variables for the Sparrow Reference Web App to run. You can complete the following
steps to do so.

1. Create a new `.env` file in the root folder of your project.
1. Copy the contents of this repo’s [.env.example](.env.example) file, and paste it in your new `.env` file.
1. Change the required values in your `.env` to your own values using the steps
   below.

#### HUB_AUTH_TOKEN

The Sparrow Reference Web App needs access to your Notehub project in order to
show the gateway and sensor nodes in your project. An access token is used to
authenticate the app.

To find retrieve an authentication token, put this in your command line (VS Code
Menu > Terminal > New Terminal), replacing `YOUR_NOTEHUB_EMAIL` &
`NOTEHUB_PASSWORD` with your own:

```
curl -X POST -L 'https://api.notefile.net/auth/login' \
    -d '{"username":"YOUR_NOTEHUB_EMAIL", "password": "NOTEHUB_PASSWORD"}'
```

When successful, you will see a response like

```
{"session_token":"BYj0bhMJwd3JucXE18f14Y3zMjQIoRfD"}
```

Copy the value after the colon to set the environment variable in `.env`, e.g.

```
HUB_AUTH_TOKEN=BYj0bhMJwd3JucXE18f14Y3zMjQIoRfD
```

#### HUB_PROJECTUID

This is the unique identifier for your project in Notehub, and has the prefix `app:`. You can find this by going to your Notehub project, clicking the **Settings** menu, and finding the **Project Information** heading which contains **Project UID**. Click the copy icon to copy this to the clipboard.

```
HUB_PROJECTUID=app:245dc5d9-f910-433d-a8ca-c66b35475689
```

#### POSTGRES\_\* and DATABASE_URL

The default for these variables are fine for development purposes. In a production
environment you'll set them to point to your production database.

### Routing

The Web App receives data from Notehub through a _Route_ created on Notehub.io
and targeting the Web App. To set up your own route you’ll need to complete the
following two steps.

1. [Create a tunnel to a server running the reference app.](#create-a-tunnel-to-a-server-running-the-reference-app)
   - Running the tunnel allows your local copy of the reference app to be accessible on the public internet. This is necessary for Notehub to route events to your local setup.
2. [Set up a Notehub route to your tunnel.](#set-up-a-notehub-route-to-your-tunnel)
   - When Notehub receives an event it can optionally route that event to other servers. In this step, you’ll have Notehub route events to your local setup via the tunnel you created in step #1.

### Create a tunnel to a server running the reference app

The Sparrow reference app contains logic to process incoming Notehub events. But in order for Notehub to forward data to your local app for processing, your local app must be accessible from the public internet.

To make your local environment accessible you must set up a tunnel. You’re
welcome to use any tunneling setup you’re comfortable using, but we recommend
[localtunnel](https://github.com/localtunnel/localtunnel) or
[ngrok](https://ngrok.com/).

#### Localtunnel

`localtunnel` is a simple free tunnel that you can run as follows. Replace `acme`
with the name of your choice.

```sh
$ npx localtunnel --port 4000 --subdomain acme
Need to install the following packages:
  localtunnel
Ok to proceed? (y) y
your url is: https://acme.loca.lt
```

You can close the tunnel with `ctrl+c`.

#### Ngrok

Ngrok is a freemium alternative to localtunnel, which requires an e-mail
signup, a modicum of setup, and in free-mode does not give you a tunnel with a
consistent domain name. **The inconsistent domain name will require you to
update your [route](#routing) each time you start your tunnel.** To use ngrok
you’ll first need to:

- [Sign up for ngrok](https://dashboard.ngrok.com/signup). (It’s free to start.)
- [Install ngrok](https://dashboard.ngrok.com/get-started/setup). (`brew install ngrok/ngrok/ngrok` works well for macOS users—and yes, `ngrok/ngrok/ngrok` is the package name recommended by Ngrok itself.)
- [Set up your ngrok auth token](https://dashboard.ngrok.com/get-started/your-authtoken).

Next, open a new terminal outside VS Code and run `ngrok http 4000`, which
creates the tunnel itself.

```
ngrok http 4000
```

If all went well, you should see a screen in your terminal that looks like the image below. ngrok is now forwarding all requests to `https://<your-id>.ngrok.io` to `http://localhost:4000`. Copy the forwarding address (shown in the red box below) to your clipboard, as you’ll need it in the next step.

![Example of ngrok running](https://user-images.githubusercontent.com/544280/161281285-0b20f600-3c88-4c81-98ea-aef7665f59d7.png)

> **NOTE**: Your `ngrok` terminal needs to stay running for the tunnel to remain active. If you close and restart `ngrok` your URL will change.

To verify everything worked correctly, you can try loading the URL you just copied in a web browser; you should see your reference app’s home page.

### Set up a Notehub route and dashboard URL to your tunnel

With a tunnel in place, your next step is to create a route in Notehub that forwards events to your local app.

To set up the route complete the following steps:

- Visit [Notehub](https://notehub.io) and open the project you’re using for your Sparrow app.
- Select **Routes** in the navigation on the left-hand side of the screen.
- Click the **Create Route** link in the top right of the screen.
- Find the **General HTTP/HTTPS Request/Response** route type, and click its **Select** button.
- Give your route a name.
- For the route **URL**, paste the localtunnel or ngrok URL you copied earlier, and append `/api/datastore/ingest`. For example your route should look something like `https://bb18-217-180-218-163.ngrok.io/api/datastore/ingest`.
- Scroll down, and click the blue **Create new Route** button at the bottom right of the page.

And with that your route is now complete. When Notehub receives an event it should automatically route that event to your tunnel, and ultimately to your local app.

> **NOTE** Event routing only happens when Notehub receives an event, therefore your Sparrow hardware needs to generate new data and send it to Notehub for Notehub to invoke your route.

### Setup a Project Dashbaord URL to your tunnel

- Visit [Notehub](https://notehub.io) and open the project you’re using for your Sparrow app.
- Select **Settings** in the navigation on the left-hand side of the screen.
- Scroll down to **Device Dashbaord URL**.
- Click the pencil icon to edit the URL, and paste the localtunnel or ngrok URL you copied earlier followed by `api/go?gateway=[device]&sensor=[sensor]&pin=[pin]`. For example, `https://bb18-217-180-218-163.ngrok.io/api/go?gateway=[device]&sensor=[sensor]&pin=[pin]`.
- Click the checkmark on the right to save the changes.

Now that you have both a tunnel, route and Device dashbaord URL in place, your last step to get up and running is to create the database itself.

### Database

The Sparrow reference web app uses PostgreSQL to store data, including the data
it receives from Notehub events. To set up your own PostgreSQL instance you need
to complete the following steps.

#### Create the database

There are many different ways you might want to create a Postgres database. If
you’re unsure how to start, we recommend running Postgres through Docker as
follows.

Open a terminal (VS Code > Terminal Menu > New Terminal) and run one of the
following commands.

```sh
# Choose one:
./dev.db.ephemeral.sh # Start a database which will delete its data when stopped
./dev.db.persistent.sh # Start a database with data that persists after stopping and starting again.
```

On Windows you may need to `Allow access` when the Windows Defender Firewall
asks you to allow `com.docker.backend.exe`.

```sh
$ ./dev.db.persistent.sh
... elided ...
Done in 2.65s.
Database is now running the background. Use ./dev.db.stop.sh to stop it.
```

This creates a PostgreSQL database running in Docker. You can ensure the database is
running as expected with the `./dev.db.status.sh` command.

```sh
$ ./dev.db.status.sh
398940737bae   postgres  "docker-entrypoint.s…"   2 hours ago   Up 2 hours   0.0.0.0:5432->5432/tcp, :::5432->5432/tcp   sparrow-postgresql-container
```

To stop the database you can use `./dev.db.stop.sh`.

```sh
./dev.db.stop.sh # Stop the database (and delete the ephemeral data if any)
```

And you can delete the database’s data with `./dev.db.delete.sh`.

```sh
./dev.db.status.sh # Show whether the database is running
./dev.db.delete.sh # to remove the persistent database data
```

If you'd like to connect to your locally running Postgres instance to ensure new
Notehub events are being added, you can use the [Prisma
Studio](https://www.prisma.io/studio) database management tool to easily
explore.

```sh
./dev.db.manage.sh # Open a webpage at http://localhost:5555 that lets you explore the database
```

This will open up a new browser window at http://localhost:5555 where you can
see your Prisma DB, its tables, and any data that currently resides therein—
whether it came from a bulk data import or was routed in by Notehub.

And just like any other database GUI, you can click into models to view data,
manipulate data, filter, query, etc.

#### Custom PostgreSQL Server

If you manually created your PostgreSQL database instead of using one of the
scripts above to automatically create one in Docker you will want to configure
the `DATABASE_*` environment varialbes in `.env` and then use the
`./prod.db.init.sh` script to initialize the database.

```
$ ./prod.db.init.sh
This script will clear your database and reinitialize it.
Continue (y/n)?y
yes
Done in 10.36s.
Database has been reinitialized.
```

#### Troubleshooting Sparrow getting data from Postgres

There are a number of gotchas that could prevent your Notehub data from making it to Postgres and your Sparrow app. If you’re having issues try the following things:

- Is Docker running the local Postgres instance on your machine?
  - Currently there's no error message thrown if the Postgres Docker container's
    not running.
- Does your Ngrok endpoint match what's in Notehub and have the suffix
  `/api/datastore/ingest`?
  - Be aware, every time the Ngrok connection is shut down and restarted, it
    will be started up with a brand new URL, so you'll need to update the route
    accordingly in Notehub to ensure data keeps flowing to it
- Have you added the correct Postgres URL and Notehub project API environment variables to
  your `.env` file?

### Web App Development

The Sparrow Reference Web App uses the [Next.js](https://nextjs.org/) web
framework to serve React-powered web pages and HTTP JSON APIs. You can start a
Next.js development server using `yarn dev`.

```
yarn dev
```

With the development server running, open <http://localhost:4000> in your web
browser to see your application.

Next.js automatically watches your project’s files, and updates your application
as you make changes. To try it, open your app’s `src/pages/index.tsx` file, make
a change, save the file, and notice how your browser automatically updates with
the change. Changes to `.env`, **not** being automatically reloaded require you
to stop the `yarn dev` with `ctrl+c` and to start `yarn dev` back up.

The project’s `src/pages/api` directory are
[APIs](https://nextjs.org/docs/api-routes/introduction) as opposed to
React-powered HTML pages.
The Sparrow Reference Web App uses these routes in several places to access the
[Notehub API](https://dev.blues.io/reference/notehub-api/api-introduction/)
server without triggering a full-page reload.

> **NOTE**: If you’re new to Next.js, the [Next.js official interactive
> tutorial](https://nextjs.org/learn/basics/create-nextjs-app) is a great way to
> learn the basics, and understand how the Sparrow Reference Web App works.

### Bulk Data Import

The Sparrow Reference Web App can do a bulk import of historic data from Notehub
to populate your database with any data from the past 10 days. This can help
when you’re first getting started, or if you want to import events you might
have missed during web app or database downtime.

To run the bulk import first make sure your web app is running, and then complete
the following steps.

- Go to <http://localhost:4000/admin/bulk-data-import>.
- Click the **Import** button.
- Wait. If you're curious, watch the detailed information in your web app’s server logs
  (in the terminal where you ran `yarn dev`).

  ![Imported 3085 items in 1 minutes.](readme.bulk.data.import.png)

## Cloud Deployment

The Sparrow Reference Web App is a Next.js project, and is therefore easily deployable to any platform that supports Next.js applications. Below are specific instructions to deploy to a handful of common platforms.

> **NOTE**: For all deployment options we recommend [creating a
> fork](https://docs.github.com/en/get-started/quickstart/fork-a-repo) of this
> repository, and performing all deployment steps on that fork.

A PostgreSQL database will be required for any of the deployment methods below.
Setting up a production PostgreSQL database is beyond the scope of this guide
but, for a price, many cloud services such as [Azure Database
Service](https://azure.microsoft.com/en-us/product-categories/databases/), [Heroku](https://www.heroku.com/postgres) or
[Amazon AWS RDS](https://aws.amazon.com/rds/) can host a PostgreSQL database for
you.

### Deploy on Netlify (recommended)

This repo contains [Netlify configuration](netlify.toml) that allows you to deploy to [Netlify](https://www.netlify.com/) with a simple button click! Click the button below to automatically fork this repo, set environment variables, and immediately to deploy to Netlify.

[![Deploy to Netlify](https://www.netlify.com/img/deploy/button.svg)](https://app.netlify.com/start/deploy?repository=https://github.com/blues/sparrow-reference-web-app)

Read our step-by-step guide to [deploying the Sparrow Reference Web App app on Netlify](https://bluesinc.atlassian.net/wiki/spaces/SPAR/pages/4686203/Deploy+Sparrow+Reference+Web+App+with+Netlify) for more
information.

> Note: Timeouts happen after 10 seconds on Netlify web requests. If you want
> to run a long Bulk Data Iimport you will want to run it on your local dev
> machine with your local `.env` file pointed at your cloud database.

### Deploy on Vercel

The next easiest way to deploy your Next.js app is to use the [Vercel Platform](https://vercel.com/new?utm_medium=default-template&filter=next.js&utm_source=create-next-app&utm_campaign=create-next-app-readme) from the creators of Next.js.

Read our step-by-step guide to [deploying the Sparrow Reference Web App app on Vercel](https://bluesinc.atlassian.net/wiki/spaces/SPAR/pages/4817057/Deploy+the+Sparrow+Starter+Web+App+Using+Vercel) for more information.

### Deploy on Microsoft Azure Cloud

> **NOTE**: Running this site as Azure Container Instances will cost about $30/mo.

Follow the steps below to deploy to [Microsoft Azure Cloud](https://azure.microsoft.com/en-us/). If you need more details on any of the steps, see
[Docker’s documentation on deploying Docker containers on Azure](https://docs.docker.com/cloud/aci-integration/).

**Build Machine and Cloud Setup**

1. Sign up for [Azure](https://azure.microsoft.com/en-us/).
1. Sign up for [Docker Hub](https://hub.docker.com/signup).
1. Install [Docker](https://docs.docker.com/get-docker/).
1. Install [docker-compose](https://docs.docker.com/compose/install/).
1. Install the confusingly named [Compose
   CLI](https://github.com/docker/compose-cli), which adds cloud-specific
   compose-like support to `docker` via a wrapper of the standard `docker` cli.
1. Check that _Compose CLI_ is working. `docker version | grep 'Cloud integration' && echo Yay || echo Boo`.
1. Sign into Azure using `docker login azure`. See <https://docs.docker.com/cloud/aci-integration/>.
1. Create a docker context on Azure named however you like. For example, `docker context create aci myacicontext`.

**Configure the _sparrow-reference-web-app_ environment**

1. `cp .env.example .env.production.local`
2. Configure the _Required_ variables and the Azure variables.

**Build and Deploy**

`./deploy.sh`

```
...
[+] Running 3/3
 ⠿ Group sparrow-reference-web-app                   Created    6.3s
 ⠿ sparrowreferencewebeapp-https-reverse-proxy  Created    113.8s
 ⠿ sparrowreferencewebeapp                      Created    113.8s
[deploy.sh] 🚀 Successful deployment.
[deploy.sh] 🔃 To deploy new changes, simply run this script again.
[deploy.sh] 🚮 To delete the deployment or see cloud details, visit the Azure Portal: https://portal.azure.com/#blade/HubsExtension/BrowseResource/resourceType/Microsoft.ContainerInstance%2FcontainerGroups
[deploy.sh] ⏰ In a few minutes the site should be visible here:
[deploy.sh] 🔜 https://mysparrowstarer.eastus.azurecontainer.io
```

## Security

Authentication and authorization are beyond the scope of this reference project.
If you add basic auth or other HTTP-header-based auth, you can
add those headers to your Notehub route to authorize it to route data to your web
app.

## Testing

The Sparrow Reference Web App contains both unit and end-to-end tests to ensure the project continues to work as intended.

### Testing with Jest

This repo contains a unit testing setup that utilizes [Jest](https://jestjs.io/) and [React Testing Library](https://testing-library.com/docs/react-testing-library/intro/). No additional installation is necessary to use these tools (`yarn install` already installed them), but there is some additional setup you must perform to run the tests.

**Unit Test Setup**

The Sparrow Reference Web App’s testing setup requires a test-specific environment variable file. Follow the steps below to create that file.

1. Create a `.env.test.local` file in the root of your project.
1. Copy the contents of the repo’s [`.env.test.local.example`](.env.test.local.example) file and paste it into your `.env.test.local` file.
1. Change the values in your `.env.test.local` file to your own values. (You can likely copy and paste them from your `.env.local` file.)

**Running Unit Tests**

This repo’s tests live in its `__tests__` folder, and you can run the full test suite using the command below.

```bash
yarn test
```

**Code Coverage from Unit Tests**

To see code coverage for the entire project, run the following command.

```bash
yarn test:coverage
```

When the command finishes, you can open the coverage report by opening the repo’s `coverage/lcov-report/index.html` file in your web browser. You can do so on macOS using the command below.

```bash
open coverage/lcov-report/index.html
```

### Testing with Cypress

The Sparrow Reference Web App uses [Cypress](https://www.cypress.io/) for automated UI & API testing.

**Cypress Setup**

To run the project’s Cypress tests you first need to perform the following setup.

1. Create a `cypress.env.json` file in the root of your project.
2. Copy and paste the code below into your newly created file.

```
{
  "gatewayUID": "dev:###############"
}
```

3. Change the placeholder `"dev:###############"` value to a valid gateway UID.

**Running Cypress Tests**

You can run the Cypress test suite with the `yarn cypress` command. `yarn cypress:run` runs the tests in your terminal.

```bash
yarn cypress:run
```

And `yarn cypress:open` launches the tests in the Cypress GUI.

```bash
yarn cypress:open
```

## Support

If you run into any issues using this repo, feel free to create an
[issue](issues/new) on this repository, or to reach out on our developer
[forum](https://discuss.blues.io/).
