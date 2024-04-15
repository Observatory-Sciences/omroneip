import os
import requests
import zipfile

def download_file(url, save_path):
    with requests.get(url, stream=True) as response:
        with open(save_path, 'wb') as f:
            for chunk in response.iter_content(chunk_size=8192):
                f.write(chunk)

def unzip_file(zip_file, extract_dir):
    with zipfile.ZipFile(zip_file, 'r') as zip_ref:
        zip_ref.extractall(extract_dir)

def main():
    github_url = 'https://github.com/libplctag/libplctag/releases/download/v2.5.5/libplctag_2.5.5_ubuntu_x86.zip'
    zip_file_name = 'libplctag.zip'
    download_path = os.path.join(os.getcwd(), zip_file_name)
    extract_dir = os.path.join(os.getcwd(), 'libplctag')

    # Download the zip file
    print("Downloading zip file...")
    download_file(github_url, download_path)

    # Unzip the file
    print("Unzipping file...")
    unzip_file(download_path, extract_dir)

    # Delete the zip file
    print("Deleting zip file...")
    os.remove(download_path)

    print("Unzip complete!")

if __name__ == "__main__":
    main()