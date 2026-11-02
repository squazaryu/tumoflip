import fs from "node:fs";
import path from "node:path";

const placeholder = Buffer.from("<app_name>");

// The copied template needs literal byte replacement, not a glob parser.
// Like the old default glob, leave hidden entries alone. Never follow links.
export function replaceTemplatePlaceholders(projectRoot, name) {
    if (typeof name !== "string" || !fs.lstatSync(projectRoot).isDirectory()) {
        throw new TypeError("Template root must be a directory and its name a string");
    }
    const replacement = Buffer.from(name, "utf8");
    const directories = [projectRoot];
    while (directories.length) {
        const directory = directories.pop();
        for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
            if (entry.name.startsWith(".")) continue;
            const file = path.join(directory, entry.name);
            if (entry.isDirectory()) {
                directories.push(file);
            } else if (entry.isFile()) {
                const bytes = fs.readFileSync(file);
                let index = bytes.indexOf(placeholder);
                if (index < 0) continue;
                const chunks = [];
                let offset = 0;
                while (index >= 0) {
                    chunks.push(bytes.subarray(offset, index), replacement);
                    offset = index + placeholder.length;
                    index = bytes.indexOf(placeholder, offset);
                }
                chunks.push(bytes.subarray(offset));
                fs.writeFileSync(file, Buffer.concat(chunks));
            }
        }
    }
}
