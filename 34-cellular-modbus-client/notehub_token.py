from pathlib import Path
import requests
import json


def get_new_token(client_id, client_secret):
    data = {
        'grant_type': 'client_credentials',
        'client_id': client_id,
        'client_secret': client_secret,
    }

    response = requests.post('https://notehub.io/oauth2/token', data=data)
    if response.ok:
        new_token = json.loads(response.text)['access_token']
        print(f"New token: {new_token}")
        return new_token
    else:
        raise Exception("Failed to get new token.")


def overwrite_token(token_file, new_token):
    path = Path(token_file)

    # Create the access.token file if it doesn't exist.
    if not path.exists():
        path.touch()

    with path.open(mode='w') as f:
        f.write(new_token)

    print(f"Wrote token {new_token} into {path}.")


def read_token(token_file):
    path = Path(token_file)
    with path.open(mode='r') as f:
        return f.read()


def need_new_token(token, project_uid):
    headers = {
        'Authorization': f'Bearer {token}',
    }
    url = f'https://api.notefile.net/v1/projects/{project_uid}/events'
    headers = {'Authorization': f'Bearer {token}'}

    response = requests.get(url, headers=headers)
    return response.status_code == 403


def refresh_token(client_id, client_secret, token_file):
    token = get_new_token(client_id, client_secret)
    overwrite_token(token_file, token)

    return token


def get_token(project_uid, client_id, client_secret, token_file):
    try:
        print(f"Reading token from {token_file}...")
        token = read_token(token_file)
        if need_new_token(token, project_uid):
            print("Need new token. Fetching new token...")
            token = refresh_token(client_id, client_secret, token_file)
    except FileNotFoundError:
        print("No token file. Fetching new token and creating token file...")
        token = refresh_token(client_id, client_secret, token_file)

    return token