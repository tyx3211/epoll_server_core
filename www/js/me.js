document.addEventListener('DOMContentLoaded', () => {
    const token = localStorage.getItem('jwt_token');
    const userInfoDiv = document.getElementById('user-info');

    if (!token) {
        // No token, redirect to login page.
        window.location.href = 'login.html';
        return;
    }

    fetch('/api/me', {
        headers: {
            'Authorization': `Bearer ${token}`
        }
    })
    .then(response => {
        if (!response.ok) {
            // Token is invalid or expired, clear it and redirect.
            localStorage.removeItem('jwt_token');
            window.location.href = 'login.html';
            throw new Error('Token validation failed');
        }
        return response.json();
    })
    .then(data => {
        if (data.user && data.user.username) {
            userInfoDiv.innerHTML = `
                <h2>欢迎, ${data.user.username}!</h2>
                <p>这是您的个人中心。</p>
                <button id="logout-btn">退出登录</button>
            `;
            document.getElementById('logout-btn').addEventListener('click', () => {
                localStorage.removeItem('jwt_token');
                window.location.href = 'index.html';
            });
        } else {
            // Should not happen if API is consistent
            userInfoDiv.innerHTML = `<p>无法获取用户信息。</p>`;
        }
    })
    .catch(error => {
        console.error('Error fetching user info:', error);
        // The redirection is already handled when the response is not ok.
    });
}); 