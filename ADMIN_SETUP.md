# AGAP staff ID review setup

The resident verification review page is available at `/admin/verifications`.

Before staff can use it, add these environment variables to the AGAP backend service in Render:

- `AGAP_ADMIN_USER`: a staff-only username
- `AGAP_ADMIN_PASSWORD`: a long, unique password

Keep these values in Render's Environment settings. Do not commit them to GitHub. Open the review page over HTTPS; the browser will prompt for the credentials. Staff can inspect a pending ID and approve or reject it. Approved residents can file complaints.

The backend accepts JPG and PNG uploads up to 5 MB. ID images are stored outside the public frontend directory and are only served through the authenticated review route.

## Storage note

The current backend stores accounts, complaints, and uploaded images as files in `Data/`. A Render service's local filesystem is not durable across replacement or redeployment unless persistent storage is configured. Use persistent storage or private object storage before collecting real resident IDs or relying on these records in production. If you attach a Render disk, set AGAP_DATA_DIR to that disk's mount path.
