use anyhow::Result;
use scraper::{Html, Selector};
use std::time::Duration;

fn client() -> Result<reqwest::blocking::Client> {
    Ok(reqwest::blocking::Client::builder()
        .user_agent("PROTECT-Suite")
        .timeout(Duration::from_secs(15))
        .build()?)
}

pub fn http_text(url: &str) -> Result<String> {
    Ok(client()?.get(url).send()?.error_for_status()?.text()?)
}

pub fn http_bytes(url: &str) -> Result<Vec<u8>> {
    Ok(client()?.get(url).send()?.error_for_status()?.bytes()?.to_vec())
}

/// Resolve a possibly-relative link against a base URL (proper RFC 3986 join).
pub fn join_url(base: &str, link: &str) -> String {
    match url::Url::parse(base) {
        Ok(b) => b.join(link).map(|u| u.to_string()).unwrap_or_else(|_| link.to_string()),
        Err(_) => link.to_string(),
    }
}

fn extract_attr(html: &str, selector: &str, attr: &str) -> Vec<String> {
    let doc = Html::parse_document(html);
    let sel = match Selector::parse(selector) {
        Ok(s) => s,
        Err(_) => return vec![],
    };
    doc.select(&sel)
        .filter_map(|e| e.value().attr(attr).map(|s| s.to_string()))
        .filter(|s| !s.is_empty())
        .collect()
}

pub fn extract_hrefs(html: &str) -> Vec<String> { extract_attr(html, "a[href]", "href") }
pub fn extract_imgs(html: &str) -> Vec<String> { extract_attr(html, "img[src]", "src") }
