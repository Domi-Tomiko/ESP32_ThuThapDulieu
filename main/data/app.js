console.log("ESP32 Camera Server JavaScript loaded");


const captureButton =
    document.getElementById("capture-button");

const filenameInput =
    document.getElementById("filename-input");

const message =
    document.getElementById("message");

const gallery =
    document.getElementById("gallery");

const downloadAllButton =
    document.getElementById("download-all-button");

const zipNameInput =
    document.getElementById("zip-name-input");

const photoPrefixInput =
    document.getElementById("photo-prefix-input");

const photoDisplayLimitInput =
    document.getElementById("photo-display-limit-input");

const autoCaptureCheckbox =
    document.getElementById("auto-capture-checkbox");

const captureIntervalInput =
    document.getElementById("capture-interval-input");

const captureCountInput =
    document.getElementById("capture-count-input");

const deletePrefixInput =
    document.getElementById("delete-prefix-input");

const deleteStartInput =
    document.getElementById("delete-start-input");

const deleteEndInput =
    document.getElementById("delete-end-input");

const deletePhotosButton =
    document.getElementById("delete-photos-button");

const deleteMessage =
    document.getElementById("delete-message");

function getZipDownloadName()
{
    let name = zipNameInput.value.trim();

    if (name === "")
    {
        name = "photos.zip";
    }

    if (!name.toLowerCase().endsWith(".zip"))
    {
        name += ".zip";
    }

    return name;
}

function getPhotoPrefix()
{
    return photoPrefixInput.value.trim();
}

function updateZipDownloadLinks()
{
    const name = getZipDownloadName();

    document
        .querySelectorAll("[data-download-all]")
        .forEach(
            (link) => {
                link.download = name;
                link.href =
                    "/download-all?name=" +
                    encodeURIComponent(name) +
                    "&prefix=" +
                    encodeURIComponent(getPhotoPrefix());
            }
        );
}

zipNameInput.addEventListener(
    "input",
    updateZipDownloadLinks
);

photoPrefixInput.addEventListener(
    "input",
    updateZipDownloadLinks
);

photoDisplayLimitInput.addEventListener(
    "input",
    loadGallery
);

downloadAllButton.setAttribute(
    "data-download-all",
    "true"
);

updateZipDownloadLinks();


/* =========================
   Capture
   ========================= */

async function captureImage()
{
    message.textContent =
        "Capturing image...";

    try {

        const requestedName =
            filenameInput.value.trim();

        const captureUrl =
            requestedName === ""
                ? "/capture"
                : "/capture?name=" +
                  encodeURIComponent(requestedName);

        const response =
            await fetch(
                captureUrl,
                {
                    method: "POST"
                }
            );

        const result =
            await response.json();

        if (
            response.ok &&
            result.success
        ) {

            message.textContent =
                "Capture successful.";

            loadGallery();

            return true;
        }
        else {

            message.textContent =
                "Capture failed.";

        }

    }
    catch (error) {

        console.error(error);

        message.textContent =
            "Communication error.";
    }

    return false;
}

captureButton.addEventListener(
    "click",
    async () => {
        captureButton.disabled = true;
        await captureImage();
        captureButton.disabled = false;
    }
);

autoCaptureCheckbox.addEventListener(
    "change",
    async () => {
        if (!autoCaptureCheckbox.checked)
        {
            message.textContent =
                "Auto capture stopped.";

            return;
        }

        captureButton.disabled = true;

        const interval = Math.max(
            0.1,
            Number(captureIntervalInput.value) || 2
        );

        const count = Math.min(
            9999,
            Math.max(1, Number.parseInt(captureCountInput.value, 10) || 1)
        );

        for (let index = 0; index < count; index++)
        {
            if (!autoCaptureCheckbox.checked)
            {
                break;
            }

            const success = await captureImage();

            if (!success)
            {
                autoCaptureCheckbox.checked = false;
                break;
            }

            if (index + 1 < count)
            {
                await new Promise(
                    (resolve) => setTimeout(resolve, interval * 1000)
                );
            }
        }

        autoCaptureCheckbox.checked = false;
        captureButton.disabled = false;
        message.textContent =
            "Automatic capture completed.";
    }
);

deletePhotosButton.addEventListener(
    "click",
    async () => {
        const prefix = deletePrefixInput.value.trim();
        const start = Number.parseInt(deleteStartInput.value, 10);
        const end = Number.parseInt(deleteEndInput.value, 10);

        if (prefix === "" ||
            !Number.isInteger(start) ||
            !Number.isInteger(end) ||
            start < 0 ||
            end < start ||
            end > 9999)
        {
            deleteMessage.textContent =
                "Enter a prefix and a valid number range.";
            return;
        }

        if (!confirm(
            `Delete ${prefix} photos from ${start} to ${end}?`
        ))
        {
            return;
        }

        deletePhotosButton.disabled = true;

        try {
            const response = await fetch(
                "/delete-photos?prefix=" +
                encodeURIComponent(prefix) +
                "&start=" + start +
                "&end=" + end,
                {
                    method: "DELETE"
                }
            );

            const result = await response.json();

            deleteMessage.textContent =
                response.ok && result.success
                    ? `Deleted ${result.deleted} photo(s).`
                    : "Delete failed.";

            if (response.ok && result.success)
            {
                loadGallery();
            }
        }
        catch (error) {
            console.error(error);
            deleteMessage.textContent =
                "Communication error.";
        }

        deletePhotosButton.disabled = false;
    }
);


/* =========================
   Gallery
   ========================= */

async function loadGallery()
{
    gallery.textContent =
        "Loading photos...";

    try {

        const response =
            await fetch("/photos");

        const result =
            await response.json();

        if (
            !response.ok ||
            !result.success
        ) {

            gallery.textContent =
                "Failed to load photos.";

            return;
        }

        gallery.innerHTML = "";

        if (
            result.photos.length === 0
        ) {

            gallery.textContent =
                "No photos.";

            return;
        }

        /*
         * Reverse the array so the newest
         * photo appears first.
         */
        result.photos.reverse();

        const displayLimit =
            Number.parseInt(
                photoDisplayLimitInput.value,
                10
            );

        if (Number.isFinite(displayLimit) && displayLimit > 0)
        {
            result.photos = result.photos.slice(
                0,
                Math.min(displayLimit, 65535)
            );
        }

        const downloadAllButton =
            document.createElement(
                "a"
            );

        downloadAllButton.textContent =
            "⬇ Download ALL";

        downloadAllButton.href =
            "/download-all";

        downloadAllButton.className =
            "download-all-button";

        downloadAllButton.setAttribute(
            "data-download-all",
            "true"
        );

        downloadAllButton.setAttribute(
            "download",
            getZipDownloadName()
        );

        updateZipDownloadLinks();

        gallery.appendChild(
            downloadAllButton
        );
        
        result.photos.forEach(
            (filename) => {

                const card =
                    document.createElement(
                        "div"
                    );

                card.className =
                    "photo-card";

                const image =
                    document.createElement(
                        "img"
                    );

                image.src =
                    "/photo?name=" +
                    encodeURIComponent(
                        filename
                    );

                image.alt =
                    filename;

                const title =
                    document.createElement(
                        "p"
                    );

                title.textContent =
                    filename;

                card.appendChild(image);

                card.appendChild(title);


                /* =========================
                Download button
                ========================= */

                const downloadButton =
                    document.createElement(
                        "a"
                    );

                downloadButton.textContent =
                    "⬇ Download";

                downloadButton.href =
                    "/download?name=" +
                    encodeURIComponent(
                        filename
                    );

                downloadButton.className =
                    "download-button";

                downloadButton.setAttribute(
                    "download",
                    filename
                );

                card.appendChild(
                    downloadButton
                );

                gallery.appendChild(card);
            }
        );

    }
    catch (error) {

        console.error(error);

        gallery.textContent =
            "Communication error.";

    }
}


/* =========================
   Initial loading
   ========================= */

loadGallery();